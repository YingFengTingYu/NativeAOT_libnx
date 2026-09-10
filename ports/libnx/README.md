# NativeAOT / libnx 移植实验

循环 GC 检查点现改为按实际 DFS 回边插入，减少内联及块重排后的误插；正负对照、压缩 GC 与独立解压验证见[第二十九轮](results/2026-09-10-round29-gc-poll-cycles.md)。

本分支基于公开的 `dotnet/runtime`，目标是直接使用 devkitPro/libnx 构建 Nintendo Switch homebrew。当前原型已经在模拟器通过基础 C#、移动 GC、终结器、显式异常和两个托管工作线程的 GC 测试；尚未完成全面运行时及游戏验证。

最新进展：实际 Lawn.Core 已在模拟器初始化并解压真实资源包，见 [第十九轮](results/2026-09-07-round19.md) 与 [第二十轮](results/2026-09-07-round20.md)。游戏画面、输入、音频和隐式硬件异常仍未验收。

已完成的实验与限制见 [第二轮](results/2026-09-07-round2.md)、[第三轮](results/2026-09-07-round3.md)、[第四轮](results/2026-09-07-round4.md)、[第五轮](results/2026-09-07-round5.md)、[第六至八轮](results/2026-09-07-round6-8.md) 和 [第九至十轮](results/2026-09-07-round9-10.md) 记录。

上下文探针：`build-context-probe.sh`，运行时使用 `run-tls-probe.ps1 -Suite Context`。内存/事件探针：`build-memory-probe.sh`，运行时使用 `-Suite Memory`。两者均需要传入固定的 `-EdenPath`，且当前均已在模拟器通过。

## 固定基线

线程资源回收及 128 次托管线程生命周期检查见 [第十一轮](results/2026-09-07-round11.md)。

默认 GC 虚拟区域为 256 MiB，完整探针连续十次独立启动通过；隐式空引用在当前模拟器仍未通过，见 [第十二轮](results/2026-09-07-round12.md)。

忙循环的编译器 GC 检查点、源码代码生成器构建与正负对照见 [第十三轮](results/2026-09-07-round13.md)。

中文文件名、FileStream 和同一句柄上的并发定位读取见 [第十四轮](results/2026-09-07-round14.md)。

线程池、Task、计时器、取消和异步文件读写见 [第十五轮](results/2026-09-07-round15.md)。

zlib、gzip、Brotli、项目同版本 ZstdSharp 与第三方依赖收集见 [第十六轮](results/2026-09-07-round16.md)。

资源校验所需的摘要后端及 SHA-256/增量摘要验证见 [第十七轮](results/2026-09-07-round17.md)。

Guid 与 CSRNG 随机数入口见 [第十八轮](results/2026-09-07-round18.md)。下一阶段接入真实项目与资源包；隐式硬件异常、游戏画面和真机仍未验收。

- 上游：`https://github.com/dotnet/runtime.git`
- 标记：`v10.0.11`
- runtime 提交：`79d0c463f1b55624c874a11585f7e47731e8d675`
- 对应 .NET 总仓库（VMR）提交：`e2f47b0110ed922f21a1522da67279133ce28f32`
- devkitPro 镜像：`devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282`

VMR 的 [source-manifest.json](https://github.com/dotnet/dotnet/blob/e2f47b0110ed922f21a1522da67279133ce28f32/src/source-manifest.json) 将 runtime 映射到上述提交。NuGet 包中的 `Microsoft.NETCore.App.versions.txt` 记录的是 VMR 提交，不能把它直接当成 runtime 仓库提交。

## 本地构建原则

IPv4 Socket、DNS、poll 异步队列、取消、HTTP 和明文 WebSocket 的公开 libnx 接入及模拟器验证见[第二十七轮](results/2026-09-08-round27-network.md)。IPv6、stock Linux BCL 网卡枚举、模拟器组播和辅助数据并未因此可用；宿主需在退出前调用网络事件静默接口。

文件路径新增保留挂载点 `/romfs` → `romfs:/`，由宿主使用公开 libnx `romfsInit()` 挂载。其余 Unix 绝对路径仍映射到 SD，存档位置保持原约定。原生接口、托管游戏验证与限制见 [第二十六轮](results/2026-09-08-round26-romfs.md)。

所有生成文件放在已被上游忽略的 `artifacts` 目录。保留原始 LICENSE 和第三方声明。每个可验证步骤分别提交，不提交 SDK、NuGet 包、模拟器或编译产物。

先验证未修改的 Linux 主机构建，再进行 libnx 交叉构建。原生库成功编译不代表 GC、异常、多线程或游戏已经可用。

## Linux 主机构建

在仓库根目录执行：

```sh
docker build -t nativeaot-libnx-build:10.0.11 ports/libnx
docker run --rm -v "$PWD:/runtime" nativeaot-libnx-build:10.0.11 bash ports/libnx/build-host.sh
```

Windows PowerShell 使用 `--mount "type=bind,source=$((Get-Location).Path),target=/runtime"` 指定仓库挂载。

主机构建只调用上游原生构建脚本，不构建托管类库或 ILC，也不引入其他平台的运行时二进制。

## ARM64 TLS 汇编验证

第一处目标端补丁为 `src/coreclr/nativeaot/Runtime/unix/unixasmmacrosarm64.inc` 增加 `TARGET_LIBNX` 分支，使用 devkitA64 的 soft thread pointer 和 local-exec TLS 重定位。测试直接包含实际运行时的汇编宏，与 C++ 编译器生成的 `thread_local` 地址比较，同时验证 x0–x7 参数寄存器、主线程和两个工作线程的隔离。

```sh
docker run --rm -v "$PWD:/runtime" nativeaot-libnx-build:10.0.11 bash ports/libnx/build-tls-probe.sh
```

Windows 使用固定版本 Eden v0.2.1 的 `eden-cli.exe`：

```powershell
.\ports\libnx\run-tls-probe.ps1 -EdenPath '模拟器的绝对路径/eden-cli.exe'
```

脚本在本仓库 `artifacts/libnx/emulator` 创建独立便携环境，复制已有的模拟器及许可证。测试分别运行修复分支与保留 Linux TLS 访问方式的负对照。前者必须通过，后者必须在地址比较处明确失败；不通过错误地址读取内存。

结果与日志保存在 `artifacts/libnx/tls-probe`。这些测试验证了真实 NativeAOT 汇编宏的适配，但不包含完整运行时启动，也不代表托管 GC 或异常已经可用。

## 上游平台配置与完整配置尝试

`eng/native/configureplatform.cmake` 和 `configurecompiler.cmake` 现在识别 `Generic` / `libnx` / `aarch64` 工具链，生成 `HOST_LIBNX`、`TARGET_LIBNX` 和 ARM64 定义，不生成 Linux 平台定义。沿用 Unix 编译/ELF 分支不表示 libnx 已具备完整 POSIX 能力。

```sh
docker run --rm -v "$PWD:/runtime" nativeaot-libnx-build:10.0.11 bash ports/libnx/build-platform-probe.sh
docker run --rm -v "$PWD:/runtime" nativeaot-libnx-build:10.0.11 bash ports/libnx/configure-runtime.sh
```

第一个命令验证实际编译宏、目标文件架构以及 API 链接检测。第二个命令使用第三轮新增的 NativeAOT 独立入口，目前已经配置成功，不再进入 CoreCLR 宿主的 GSS/Kerberos 依赖。诊断保存在 `artifacts/log/libnx/cross-configure.log`。

执行 `bash ports/libnx/build-runtime.sh` 可编译当前原型运行时及最小 `System.Native`。使用 `link-managed-probe.sh` 链接时，需通过 `LIBNX_MANAGED_OBJECT` 和 `LIBNX_PROBE_HOST` 指定托管目标文件与原生入口。编译托管代码必须引入 `Libnx.NativeAOT.targets` 的 `--noinlinetls` 设置。当前产物仍不是覆盖完整 API 的正式 runtime-pack。
