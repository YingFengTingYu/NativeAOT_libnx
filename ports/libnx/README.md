# NativeAOT / libnx 移植实验

本分支基于公开的 `dotnet/runtime`，目标是直接使用 devkitPro/libnx 构建 Nintendo Switch homebrew。当前处于源码构建与 ABI 验证阶段，尚未实现可运行的 NativeAOT/libnx 运行时。

已完成的实验与限制见 [第二轮记录](results/2026-09-07-round2.md)。

## 固定基线

- 上游：`https://github.com/dotnet/runtime.git`
- 标记：`v10.0.11`
- runtime 提交：`79d0c463f1b55624c874a11585f7e47731e8d675`
- 对应 .NET 总仓库（VMR）提交：`e2f47b0110ed922f21a1522da67279133ce28f32`
- devkitPro 镜像：`devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282`

VMR 的 [source-manifest.json](https://github.com/dotnet/dotnet/blob/e2f47b0110ed922f21a1522da67279133ce28f32/src/source-manifest.json) 将 runtime 映射到上述提交。NuGet 包中的 `Microsoft.NETCore.App.versions.txt` 记录的是 VMR 提交，不能把它直接当成 runtime 仓库提交。

## 本地构建原则

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

第一个命令验证实际编译宏和目标文件架构。第二个命令目前预期返回非零：上游 CoreCLR 配置还会进入 `Corehost.Static`，请求 libnx 没有的 GSS/Kerberos 原生依赖。由于工具链限制目标库搜索范围，它不会误用宿主 Linux 的 `libkrb5-dev`。诊断保存在 `artifacts/log/libnx/cross-configure.log`。

下一步需要为 NativeAOT 整理独立于 CoreCLR 宿主的原生构建依赖，并继续实现 libnx 平台层；当前不能发布完整 libnx runtime-pack。
