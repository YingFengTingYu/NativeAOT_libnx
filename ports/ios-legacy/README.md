# NativeAOT：旧版 iOS ARM64 适配

本目录从现有 `ios-arm64` NativeAOT 向下适配 iOS 7，不依赖 macios 托管绑定。当前已构建运行时、`System.Native` 和 C# 命令行探针；同一套 pthread TLS 实现在 Apple Silicon Mac 和 iOS 10.0.2 的 iPad mini 4 上均通过全部 7 组功能测试。**尚未在真实 iOS 7 设备上验证，不能据此宣称完整支持 iOS 7。**

## 固定基线与分支

- 官方仓库：<https://github.com/dotnet/runtime>
- 基线：`v10.0.7`，提交 `7706f546bac1a99b3d891afe3591dc88c67f0cc4`。
- 官方托管库和运行时包版本：`10.0.7`。
- 分支：`codex/legacy-ios-arm64`。
- 原生构建：Xcode 26.3 / Apple Clang 17、iOS 9.3 SDK、最低部署版本 7.0。

现有 `YingFengTingYu/NativeAOT_libnx` 已经是官方仓库的 fork，可以在其中维护这个独立分支，无需重新 fork。首次获取时可按下面的方式建立本地分支；已有本目录时不必重复克隆：

```bash
git clone --branch v10.0.7 --single-branch --origin upstream https://github.com/dotnet/runtime.git runtime-ios-legacy
cd runtime-ios-legacy
git remote add origin https://github.com/YingFengTingYu/NativeAOT_libnx.git
git switch -c codex/legacy-ios-arm64
```

## 适配范围

1. 使用 Mach 时钟为缺少 `clock_gettime` 的旧系统提供单调计时，保留条件变量超时和虚假唤醒处理。
2. iOS 7 使用系统 `arc4random_buf` 提供安全随机数，避免依赖 iOS 8 才引入的 `CCRandomGenerateBytes`。
3. iOS 7 缺少编译器原生 TLS 支持，因此使用 pthread key 保存运行时线程状态、随机采样状态、回调上下文和诊断状态；在线程退出时先完成运行时清理，再释放 TLS 存储。
4. 沿用上游模拟 TLS 的汇编入口，额外保存调用者整数和完整 SIMD 寄存器，避免首次初始化和原生回调损坏参数。
5. 旧 SDK 使用既有 Mach 线程状态查询后备路径；运行时页大小与固定大小的回调代码块分别处理。
6. 补齐旧 SDK 未暴露的 Darwin 网络类型常量，并修正基础库对旧系统时钟接口的假设。
7. 为 ILCompiler 添加显式的 Mach-O 最低版本参数；未指定时保留上游默认值。

这不是完整的旧 iOS runtime-pack。当前探针没有使用 CryptoKit、Network.framework 或 Swift 支持库，因此只在探针的链接配置中移除了这些现代框架。完整加密、TLS/HTTP、非 invariant 全球化、游戏集成和 ARM32 尚未验收。

## 环境

需要 .NET 10 SDK、当前 Xcode、CMake、Python 3；本机验证需要 Apple Silicon。原生构建的其他依赖见上游 [macOS 构建要求](../../docs/workflow/requirements/macos-requirements.md)。

从 Xcode 7.3.1 的磁盘映像复制完整目录：

```text
Xcode.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS9.3.sdk
```

默认放到 `$HOME/SDKs/iPhoneOS9.3.sdk`。构建脚本会由原始 `SDKSettings.plist` 补齐新版 Clang 读取的 `SDKSettings.json`，保留原始 SDK 内容。旧 SDK 没有现代 C++ 标准库头文件，脚本显式采用当前 iOS SDK 的 libc++ 头文件；系统 C 头文件和链接库仍来自旧 SDK。

## 构建与验证

先按上游 iOS NativeAOT 的工作流建立标准基线。本分支实际执行过以下命令并通过：

```bash
./build.sh --cross -s clr.alljits+clr.tools+clr.nativeaotruntime+clr.nativeaotlibs+libs -c Release -os ios -arch arm64
```

构建本地 ILCompiler，以及 iOS 7 运行时与基础库：

```bash
./build.sh clr.tools -c Release -os ios -arch arm64 --cross
python3 ports/ios-legacy/build-runtime.py --platform ios
python3 ports/ios-legacy/build-probe.py --platform ios
python3 ports/ios-legacy/audit-probe.py
python3 ports/ios-legacy/test-compiler.py
```

SDK 位于其他目录时，给两个构建脚本都传入 `--sdk /绝对路径/iPhoneOS9.3.sdk`。

构建和运行本机对应实现：

```bash
python3 ports/ios-legacy/test-native.py
python3 ports/ios-legacy/build-runtime.py --platform osx
python3 ports/ios-legacy/build-probe.py --platform osx
./artifacts/legacy-ios/probes/osx/publish/LegacyIOSProbe
```

探针也可单独运行一组测试，例如：

```bash
./artifacts/legacy-ios/probes/osx/publish/LegacyIOSProbe callbacks
```

可用名称为 `clock`、`gc`、`exceptions`、`threads`、`callbacks`、`tasks`、`files`。全部运行时应显示 `PASS ALL (7 suites)` 并返回 0。

所有二进制、SDK 暂存、NuGet 文件、日志和设备信息均放在被忽略的 `artifacts` 或仓库外目录，不应提交。

## 接入其他托管项目时的必要设置

必须同时使用修改后的原生运行时与本地 ILCompiler，并禁用托管代码中的内联 TLS。仅设置 `AppleMinOSVersion=7.0` 不够。`build-probe.py` 演示了 `IlcToolsPath`、`IlcSdkPath`、`IlcFrameworkNativePath` 与 `SysRoot` 的组合。

```xml
<ItemGroup>
  <IlcArg Include="--noinlinetls" />
  <IlcArg Include="--macho-minimum-os-version:7.0" />
</ItemGroup>
```

若使用本分支的 MSBuild 集成文件，也可用 `IlcMachOMinimumOSVersion` 属性传入版本。该参数只决定 Mach-O 版本记录；系统兼容性仍由对应的原生运行时和基础库负责。

## 真机验证

最终确认 iOS 7 的最低兼容性，需要真实 iOS 7 ARM64 设备。iOS 10 的 iPad mini 4 可以先验证 ARM64 上的启动、GC、异常、线程、回调和文件路径，但不能替代 iOS 7 测试。

当前产物是命令行探针：

```text
artifacts/legacy-ios/probes/ios/publish/LegacyIOSProbe
```

本机可以为越狱设备的测试产物生成 ad-hoc 签名；这不是普通设备的开发者签名或安装授权：

```bash
codesign --force --sign - --digest-algorithm=sha1,sha256 artifacts/legacy-ios/probes/ios/publish/LegacyIOSProbe
codesign --verify --strict artifacts/legacy-ios/probes/ios/publish/LegacyIOSProbe
```

本轮已执行并通过本机签名检查，上述双摘要产物也已在 Meridian 越狱的 iOS 10.0.2 真机上运行通过。新版工具会提示 SHA-1 即将弃用；这里保留双摘要用于旧系统兼容实验。

已越狱设备可以使用 SSH 传入和启动，不依赖新版 Xcode 的 Developer Disk Image。需要设备处于已激活的越狱状态并运行 SSH 服务。USB 已配对与 SSH 可用是两个独立条件；设备重启后应先确认越狱与 SSH 的状态。

这台 Meridian 设备从应用数据目录或 `/var/tmp` 启动新程序时，内核记录 `System Policy: deny process-exec`，Shell 返回 `Operation not permitted`（126）。普通 C 程序和系统 `ls` 的副本也出现相同行为；将同一产物复制到 `/usr/local/libexec` 下的测试目录后可以运行。因此，出现这一错误时应先检查执行位置与系统日志，不能直接判断为 NativeAOT 不兼容。

本轮通过 USB House Arrest 传入 Meridian 的数据容器，再由 Filza 的 root Shell 执行。进入设备上放有 `LegacyIOSProbe` 的目录后，使用以下方式启动；这些命令在 iPad 上执行，不是在 Mac 上执行：

```sh
probe_install_dir=/usr/local/libexec/NativeAOTProbe-fbbefea22
mkdir -p "$probe_install_dir"
cp ./LegacyIOSProbe "$probe_install_dir/LegacyIOSProbe"
chmod 755 "$probe_install_dir/LegacyIOSProbe"
"$probe_install_dir/LegacyIOSProbe" > run.log 2>&1
probe_status=$?
printf '%s\n' "$probe_status" > exit-code.txt
cat run.log
```

实测结果为 `PASS ALL (7 suites)`，退出码 0；普通 C 探针测得该设备页大小为 16384 字节。上述目录限制是此设备上的实际观察，不代表所有越狱或所有旧版 iOS 都采用相同策略。具体环境与覆盖范围见 [验证记录](results/2026-09-08.md)。

实机执行时保存完整输出和退出码。成功链接、静态检查通过、本机测试通过都不等同于实机通过。

当日的构建与验证记录见 [2026-09-08](results/2026-09-08.md)。
