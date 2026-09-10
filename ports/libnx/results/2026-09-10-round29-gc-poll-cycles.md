# 第二十九轮：按实际控制流环减少 GC 检查点

原先以 `successor->bbNum <= block->bbNum` 标记循环。基本块编号是身份编号，内联和重排后的无环分支也会满足这个条件，导致解压等计算代码误插 GC 检查。

现在使用上游 `fgComputeDfs` 和 `FlowGraphDfsTree::IsAncestor` 标记实际 DFS 回边，覆盖自环、嵌套环和不可约环，不依赖自然循环的支配关系。重新计算前失效旧 DFS、支配树和相关分析；先完成标记，再沿用上游检查点降低及 GC/EH 元数据生成。只在 ARM64 NativeAOT 且显式启用 `LibnxLoopGcPolls` 时生效。没有删除该开关或改变 GC 暂停实现。

## 编译与反汇编

Linux x64 宿主交叉代码生成器构建通过。新文件 SHA-256 为 `50BCC75F2763C9D64A0152FFED74041EEEB717FF37E13EF29C876609377DDB36`。

使用游戏同版本 ZstdSharp.Port 0.8.8，三个主要解压函数的 GC helper 静态调用位置从 31/70/81 减为 5/14/27，机器码字节数从 3268/7128/12364 减为 2444/5460/9952。HUF_selectDecoder、ZSTD_decodeSeqHeaders、ZSTD_decompressBlock_internal 三个无环回归示例的额外检查消失。这些位置数不是实际执行次数，也不意味着每处都发生 GC。

## GC 正负对照

新增 `ports/libnx/tests/gc-polls` 与构建、运行脚本。探针让工作线程执行没有分配或方法调用的忙循环，主线程强制进行三次压缩 GC；独立原生看门狗两秒后才解除循环，用于区分成功响应 GC 与超时逃生。

覆盖普通循环、嵌套循环、双入口 goto（两个入口均测试）、switch 状态循环、try/finally 和 catch 中的循环。每种情况都验证看门狗没有触发、数组确实移动、内容保持正确，并检查 finally 执行。另测 64 个终结器和 64 次线程启动/回收。

最终正例 NRO SHA-256：`62E96B7369F58C2EBD52DA237EB6D5EC8AC8C163495A3ED8CEB6346070DB33E4`。固定 Eden v0.2.1 CLI 连续三个进程全部通过，均正常结束，结果归档在 `artifacts/libnx/gc-poll-probe/pass-1` 至 `pass-3`。

相同源码关闭循环检查的负例 SHA-256：`47B4CDC8B567400113CD049EC92B5E1FD236562A764210995CD10E6993669190`。首个忙循环按预期触发原生看门狗，`stage.80=1`、`stage.100=10`，证明探针能发现原来的 GC 阻塞。不能把这个关闭版本用于普通游戏。

```powershell
docker run --rm --mount "type=bind,source=$PWD,target=/runtime" lawn-switch-build:10.0.11 bash /runtime/ports/libnx/build-gc-poll-probe.sh
.\ports\libnx\run-gc-poll-probe.ps1 -EdenPath '固定版本 Eden 的 eden-cli.exe 绝对路径'
```

负对照构建增加 `LIBNX_GC_POLLS=false` 和独立 `LIBNX_GC_PROBE_OUTPUT`；运行脚本对应指定 `-ProbeDirectory` 与 `-NegativeControl`。构建需要带 .NET 10 SDK 的 devkitA64 容器、已构建的运行时和宿主 JIT，以及生成的 interop/directpinvoke.txt；这里只提供命令，不把游戏仓库的镜像当成 runtime 仓库自动提供的依赖。

## 独立解压性能

用游戏真机测试的同一 RSB 提取选关主体 32 MiB 和中文资源 2 MiB 的压缩数据。输入预载，解密、上下文创建、摘要验证不计时。新旧检查策略使用相同程序、同版 ILC/类库和相同参数；两种模式各启动三个进程，每次两轮预热、九轮测量，中间反转执行顺序。

环境为 Linux NativeAOT 10.0.11 + QEMU 7.2 ARM64 用户态仿真，CPU 模型 cortex-a57，并非 Switch 真机：

| 数据 | 旧规则中位数 | 新规则中位数 | 耗时降低 |
| --- | ---: | ---: | ---: |
| 选关主体 32 MiB | 222.93 ms | 183.04 ms | 17.89% |
| 中文资源 2 MiB | 6.02 ms | 4.66 ms | 22.56% |

没有剔除离群值；计时期间没有托管分配或 GC，各进程输出 SHA-256 一致。全部数据、复现源码与汇编位于游戏仓库 `artifacts/switch/gc-poll-benchmark`。仿真收益不能等同于真机收益；完整游戏与真机性能分别验收。

## 社区 Android 封装的区别

对照 ChanseyIsTheBest/pvz_ultimate_nx 的 04ff4d35d49435b4c42523741a44b026b926a15f，公开 runtime_glue.c 关闭并发/server GC，依赖回收时其他线程已处于原生 P/Invoke。pthread_kill 的 GC 暂停请求仅计数并返回成功，没有真正实现信号暂停。该封装直接运行 Android 二进制，没有加入我们的循环检查策略；其假设不适合直接替代本端经过忙循环测试的暂停能力。
