# 第三十轮：外部暂停线程与寄存器引用固定原型

日期：2026-09-28。基线为 `0550644676c0872f4527d550af5124be4d28678b`。本轮在运行时仓库实现原型，没有修改游戏发布配置。

## 实现范围

宿主在首次进入托管代码、运行时初始化之前设置 `DOTNET_LibnxExternalGcSuspend=1`，才能启用此机制。未设置时保留原有返回地址劫持和循环检查路径。原型仅接管非并发 Workstation GC；Server GC、并发 GC 和非 GC 的线程暂停继续使用原路径。

`PalHijack` 暂停线程、获取寄存器后，先尝试 `Thread::TryLibnxExternalSuspension`。只有线程仍处于托管执行、允许 GC、SP 合法且对齐，并且当前 IP 同时具有精确 GC 安全点和可展开信息时才接管。原生系统调用、函数序言/尾声等不合适的位置会恢复线程并沿用既有劫持路径，之后可以重试。因此本轮没有证明所有代码位置都能立即暂停。

成功时把上下文保存在预分配的线程字段中，发布 `INTERRUPTED_THREAD_MARKER` 作为 GC 栈扫描入口，并让内核线程持续暂停到回收结束。接管过程不分配内存、不记录日志、不获取额外运行时锁。

扫描根时，寄存器 x0–x28 与 LR 中可能指向 GC 堆的值，沿用上游 `EnumGcRefConservatively` 以 `GC_CALL_INTERIOR | GC_CALL_PINNED` 报告。这允许对象内部引用，并避免 GC 移动这些对象后需要写回真正的 CPU 寄存器。栈、对象字段和其余根仍按正常机制扫描和修正；没有启用全局保守栈扫描，也没有全局禁止对象移动。

GC 完成后，先发布写入、清除暂停请求，再恢复外部暂停的线程。恢复前再次读取内核上下文，与交给 GC 的快照逐字节比较；若快照被改写或无法读取，就 fail-fast，不恢复到可能含过期引用的执行状态。

`LibnxThreads.c` 增加受注册表锁保护的暂停状态。`LibnxFlushThreadWrites` 跳过已经暂停的线程，避免现有“暂停—读取—恢复”刷新过程在 GC 扫描期间提前恢复它们。此行为有独立的原生线程计数器测试。

## 构建与复现

本机使用已有的 `lawn-switch-build:10.0.11` 镜像；它提供 .NET 10 SDK 和 devkitA64，不是本仓库自动构建的基础镜像。另需此前生成的 ARM64 宿主 JIT 与 `artifacts/libnx/interop/directpinvoke.txt`。在仓库根目录执行：

```powershell
docker run --rm --mount "type=bind,source=$PWD,target=/runtime" -w /runtime lawn-switch-build:10.0.11 bash ports/libnx/build-runtime.sh
docker run --rm --mount "type=bind,source=$PWD,target=/runtime" -w /runtime lawn-switch-build:10.0.11 bash ports/libnx/build-gc-external-probe.sh
.\ports\libnx\run-gc-poll-probe.ps1 -EdenPath '固定版本 Eden 的 eden-cli.exe 绝对路径' -ExternalSuspend
```

`build-gc-external-probe.sh` 强制 `LibnxLoopGcPolls=false`，选用启用新机制的原生入口，输出到 `artifacts/libnx/gc-external-probe`。默认的 `build-gc-poll-probe.sh` 继续构建旧检查点机制的回归用例。

构建负对照时使用默认原生入口，并设置 `LIBNX_GC_POLLS=false` 和独立的 `LIBNX_GC_PROBE_OUTPUT`。运行对应目录时使用 `-NegativeControl`，预期第一个忙循环触发原生看门狗。它只用于测试，不能发布为游戏运行时配置。

## 已执行验证

- 原生运行时与托管探针构建、链接通过；运行时仅出现原有的 `__security_cookie` extern 初始化警告。
- 无额外循环检查的正例在固定 Eden v0.2.1 CLI 中连续三个独立进程通过。
- 每次正例覆盖普通循环、嵌套循环、不可约 goto 的两个入口、switch、try/finally 和 catch；七个场景各执行三次强制压缩 GC。要求暂停计数确实增加、看门狗未触发、数据正确、finally 执行，且独立对照对象确实移动。
- 只保留对象内部 byref 的单线程用例执行 32 次压缩 GC；两个此类忙循环线程加一个反复 `Thread.Sleep` 的线程，再执行 32 次压缩 GC。两个用例均通过。
- 反汇编确认 `BusyInterior` 的引用保存在 x1，热循环只有取值、运算和分支，没有调用或 GC 轮询。汇编保存在探针输出的 `interior.asm.txt`。
- 64 个终结器、64 次托管线程启动/退出均通过。
- 新机制关闭、保留原循环检查的回归通过，外部暂停计数为零。
- 新机制关闭、同时关闭循环检查的负对照按预期触发看门狗：`stage.80=1`、`stage.100=10`。
- 原生 Threads 探针通过，新增 `thread.flush_preserves_pause=1` 验证刷新写入后线程计数器仍停止；随后可以恢复，64 个原生线程回收也通过。

首次验证中，紧邻固定对象的对照数组未移动。GC 可以把相邻活对象视作同一个固定区段；测试改为在两者之间制造实际可回收的间隔，再要求对照对象移动。该调整没有放宽对象移动验收，也没有修改运行时来满足地址断言。

最终测试产物 SHA-256：

| 配置 | NRO SHA-256 |
| --- | --- |
| 外部暂停启用，额外循环检查关闭 | `BF69586002C9D7F80DD6F8CB3F7AB287C3FA3E1138661CD2126EE4E07BC4FA0C` |
| 外部暂停关闭，原检查点启用 | `BDA99B7D0DEBC52FAA484908C4A7DD35C1C6D18AE52532C6FD52A2B3343CC836` |
| 两者均关闭的负对照 | `C071323AF56140E135E5A824AB7E5A3F8E002B684ECBA11D226C4A2880ACF5E8` |

正例日志和结果位于 `artifacts/libnx/gc-external-probe/pass-1` 至 `pass-3`，另外两组位于 `gc-external-baseline` 和 `gc-external-negative`。原生线程日志位于 `artifacts/libnx/threads-probe`。测试脚本明确记录 `HardwareVerified=false`。

## 尚未验证与代价

本轮证明这条路径能在模拟器上支撑上述无检查点忙循环和移动 GC；尚未完成 Switch 真机、完整游戏长期运行、所有运行时边界以及资源加载性能验收。未构建其他平台：接管和固定逻辑均限制在 `TARGET_LIBNX` 内。

寄存器采用保守识别，整数碰巧落在堆地址范围内时，也可能额外保活和固定对象，增加内存滞留与碎片。每个线程多保存一份完整上下文，每次外部暂停还增加恢复前的上下文核对。需要测量长期内存表现、GC 暂停时间和 group 加载各阶段，才能决定是否用于游戏。

此次没有修改编译器默认插入规则或正式游戏的 `LibnxLoopGcPolls` 设置。已有安全点不足的长计算区间、稀有展开/EH 路径、并发/Server GC 都不在本轮通过范围内。

## 后续游戏实验接入

同日，`PvZ_Ultimate` 仓库增加显式 `-ExperimentalExternalGc` 构建参数，在宿主首次进入托管代码前启用此原型，并关闭额外循环检查；普通发布保持原策略。完整 ARM64 构建、NRO 资源校验和实验 NSP 内容校验通过。OpenGL NRO 的标题、主菜单和冒险选关验证中，首轮完成 53 次主动压缩 GC，最后完整 GC 累计 83 次、外部暂停 2 次。此结果属于有限的游戏集成验证，不代表真机长期稳定性或加载提速。

游戏实验包 SHA-256 为 `53872D099E5CD887B5BE104FA6619507372205EBBE24FFF18D6E375131790103`。用户反馈加载仍慢，尚无真机分段计时，不能把 GC 检查点认定为加载瓶颈。根据维护者要求，模拟器游戏测试仅使用 OpenGL，deko3d 留给真机验收。
