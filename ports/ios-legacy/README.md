# NativeAOT：旧版 iOS ARM 适配

本目录从现有 `ios-arm64` NativeAOT 向下适配 iOS 7，不依赖 macios 托管绑定。当前已构建 ARM64 与 ARMv7 的运行时、`System.Native` 和 C# 命令行探针；两种架构的普通项目示例及全部 8 组功能测试均在 iOS 10.0.2 的 iPad mini 4 上通过，Apple Silicon Mac 回归也通过。**尚未在真实 iOS 7 设备上验证，不能据此宣称完整支持 iOS 7。**

普通项目发布入口、第 8 组 `interop` 测试及 ARM32 原生结构体布局与封送修复，见 [项目发布与互操作验证记录](results/2026-09-09-project-interop.md)。

## 固定基线与分支

- 官方仓库：<https://github.com/dotnet/runtime>
- 基线：`v10.0.7`，提交 `7706f546bac1a99b3d891afe3591dc88c67f0cc4`。
- 官方托管库和运行时包版本：`10.0.7`。
- 分支：`codex/legacy-ios-arm64`。
- ARM32 实验分支：`codex/legacy-ios-arm32`，真机结果见 [ARM32 验证记录](results/2026-09-09-arm32.md)。
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

这不是完整的旧 iOS runtime-pack。当前探针没有使用 CryptoKit、Network.framework 或 Swift 支持库，因此只在探针的链接配置中移除了这些现代框架。混合/嵌套结构体与回调已有真机覆盖；其他 ABI 情形、完整加密、TLS/HTTP、非 invariant 全球化和游戏集成尚未验收。

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

可用名称为 `clock`、`gc`、`exceptions`、`threads`、`callbacks`、`interop`、`tasks`、`files`。当前全部运行时应显示 `PASS ALL (8 suites)` 并返回 0。

`interop` 检查 C/C# 的结构体大小和偏移、结构体传参和返回、寄存器/栈拆分结构体、浮点聚合、`Pack=1`、嵌套结构体、`ref/out`、混合浮点参数及特殊浮点值，并运行带 GC 和嵌套 P/Invoke 的委托与 `UnmanagedCallersOnly` 回调（包括外部 pthread）。

所有二进制、SDK 暂存、NuGet 文件、日志和设备信息均放在被忽略的 `artifacts` 或仓库外目录，不应提交。

## ARM32 实验构建

ARM32 目标为 iOS 7 ARMv7。已经构建并链接原生运行时、CoreLib、基础库和两个 NativeAOT 探针；在 iOS 10.0.2 的 iPad mini 4 上，ARM32 NativeAOT 已通过全部八组测试，包括参数传递、结构体封送和原子变量对齐回归。iOS 7 真机兼容性仍未验证。

先构建在 Apple Silicon Mac 上运行的编译器和 ARM 代码生成后端，再构建目标运行时。第一条命令的 `arm64` 是宿主工具构建配置；后续 `arm` 才是待运行程序的目标架构。

```bash
./build.sh clr.alljits+clr.tools -c Release -os ios -arch arm64 --cross
python3 ports/ios-legacy/build-runtime.py --arch arm
./build.sh clr.nativeaotlibs+libs.sfx -c Release -os ios -arch arm --cross -p:IntermediatesDir="$PWD/artifacts/obj/coreclr/ios.arm.Release/legacy-ios/"
python3 ports/ios-legacy/build-arm32-probe.py --minimal --sign
python3 ports/ios-legacy/build-arm32-probe.py --sign
python3 ports/ios-legacy/audit-probe.py --arch arm --minimal
python3 ports/ios-legacy/audit-probe.py --arch arm
```

ARM32 脚本直接使用源码构建的 `ios.arm` CoreLib 和基础库，不依赖不存在的官方 `ios-arm` NativeAOT runtime-pack。它同时生成链接映射和各阶段日志，输出位于：

- `artifacts/legacy-ios/probes/ios-arm/minimal/ArmHello`：托管入口与一次 P/Invoke。
- `artifacts/legacy-ios/probes/ios-arm/full/LegacyIOSProbe32`：当前八组测试，回调组检查 64 位参数处于奇数寄存器槽及横跨寄存器/栈时的 ABI，互操作组覆盖结构体与浮点。

设备启动方式沿用下文的系统目录方案。ARM32 探针可以放在 `/usr/local/libexec/NativeAOTProbe-arm32`，并记录完整输出及退出码。本轮实机结果与 ABI 修复详见 [2026-09-09 ARM32 验证记录](results/2026-09-09-arm32.md)。

## 接入其他托管项目时的必要设置

### 普通 `.csproj` 发布入口

完成上文对应架构的运行时、基础库和 ILC 构建后，可以直接发布普通 `net10.0` 控制台项目。项目无需引用 macios，也无需添加本地工具链路径：

```bash
python3 ports/ios-legacy/publish-project.py /绝对路径/MyApp.csproj --arch arm --sign
python3 ports/ios-legacy/publish-project.py /绝对路径/MyApp.csproj --arch arm64 --sign
```

`--arch arm` 表示 ARMv7；最低系统固定为 iOS 7.0。两个架构都使用对应源码构建的 CoreLib、基础库和旧系统原生运行时。托管阶段由 .NET SDK 构建原项目，保留 `Compile` 项、项目引用、纯托管 NuGet 依赖、源生成器、嵌入资源和普通发布内容，再交给本地 ILC 和旧 SDK 链接。

可选参数：

- `--sdk /路径/iPhoneOS9.3.sdk`：选择旧 SDK 的位置。
- `--output /专用输出目录`：保存本次构建；不同项目与架构必须使用不同目录。
- `--configuration Release`：项目构建配置。
- `--framework net10.0`：多目标项目必须显式选择这个目标。
- `--native-source callback.c`：编译并链接 C / Objective-C 文件，可重复。
- `--native-library libExample.a`：链接已构建的 `.a` / `.o`，检查目标架构，可重复。
- `--link-framework UIKit`：额外链接 Apple framework，可重复。
- `--direct-pinvoke MyNativeLibrary`：静态解析指定的 P/Invoke 库名，可重复。
- `--sign`：越狱测试用 ad-hoc 双摘要签名，并验证签名。

也会读取项目的 `NativeLibrary`、`DirectPInvoke`、`IlcArg` 和 `RuntimeHostConfigurationOption`。目标架构、最低版本、pthread TLS、Workstation GC 和必要特性开关由入口统一设置，不接受冲突配置。托管阶段 `RuntimeIdentifier` 为空；需要区分平台的项目应使用独立项目或项目自身的显式构建配置，不应依赖此阶段的 `ios-arm` RID 条件。

入口向项目提供 `LegacyIOSArchitecture`（`arm` 或 `arm64`）和 `LegacyIOSMinimumOSVersion`（`7.0`）属性，可用于条件编译原生接口声明。例如：

```xml
<PropertyGroup Condition="'$(LegacyIOSArchitecture)' == 'arm'">
  <DefineConstants>$(DefineConstants);LEGACY_IOS_ARM32</DefineConstants>
</PropertyGroup>
```

Apple ARM32 的原生结构体默认按 4 字节打包，但托管结构体仍保留 .NET 的布局规则，以满足 64 位原子操作的对齐需求。`DllImport` 和委托封送会转换布局不同的结构体。`UnmanagedCallersOnly`、原始指针和直接 `calli` 不执行这种转换，接口类型必须明确匹配原生布局：ARM32 上含 `double` / `long` 的结构体通常需要 `Pack=4`；ARM64 应按对应的原生声明设置。示例测试通过上述编译条件区分两种架构。不要为所有托管结构体统一改成 `Pack=4`。

成功时打印 `publish/` 下的可执行文件路径。请把**整个 `publish/` 目录的内容**一起传到设备，普通内容文件需要和程序一起发布。`build-manifest.json` 记录哈希、架构、特性配置和构建日志目录；每次构建另存工作目录，失败时保留前一次成功产物。静态审计自动检查最低版本、依赖、TLS 和 ARM32 展开表；清单中的 `device_tested` 保持 `false`，不会把编译成功当成真机成功。

当前范围是普通 `net10.0`、`OutputType=Exe` 项目，`.app` 由下文的独立打包入口生成。iOS workload 项目、原生共享库输出、卫星资源程序集和含 RID 专属资产的 NuGet 包尚未接入，遇到这些输入会明确报错。加密、TLS/HTTP 与非 invariant 全球化仍不在已验证范围。此入口固定使用 invariant 全球化和 Workstation GC。

仓库中的普通项目示例覆盖项目引用、传递 NuGet 依赖、`LibraryImport` 源生成、嵌入资源和内容文件：

```bash
python3 ports/ios-legacy/publish-project.py ports/ios-legacy/examples/Hello/Hello.csproj --arch arm --sign
python3 ports/ios-legacy/publish-project.py ports/ios-legacy/examples/Hello/Hello.csproj --arch arm64 --sign
```

示例正常运行时输出 `PASS project / NuGet / LibraryImport / resource / content`。

### 手动接入

必须同时使用修改后的原生运行时与本地 ILCompiler，并禁用托管代码中的内联 TLS。仅设置 `AppleMinOSVersion=7.0` 不够。`build-probe.py` 演示了 `IlcToolsPath`、`IlcSdkPath`、`IlcFrameworkNativePath` 与 `SysRoot` 的组合。

```xml
<ItemGroup>
  <IlcArg Include="--noinlinetls" />
  <IlcArg Include="--macho-minimum-os-version:7.0" />
</ItemGroup>
```

若使用本分支的 MSBuild 集成文件，也可用 `IlcMachOMinimumOSVersion` 属性传入版本。该参数只决定 Mach-O 版本记录；系统兼容性仍由对应的原生运行时和基础库负责。

## UIKit 应用与 `.app` 打包

`examples/UIKit` 是普通 `net10.0` 项目，由 C# 直接 P/Invoke UIKit、Foundation 和 Objective-C runtime，不依赖 macios。它使用 `UIApplicationMain` 和动态注册的应用代理，显示计数按钮、GC 按钮和前后台状态。ARM32 与 ARM64 均已在本轮 iPad 上启动和显示，运行结果及原始快照见 [UIKit 真机记录](results/2026-09-09-uikit.md)。

```bash
python3 ports/ios-legacy/build-uikit-probe.py --arch arm
python3 ports/ios-legacy/build-uikit-probe.py --arch arm64
```

输出位于 `artifacts/legacy-ios/uikit/ios-arm/package/` 与 `ios-arm64/package/`：

- `NativeAOTUIKit32.app` / `NativeAOTUIKit64.app`：含图标、Info.plist 和 ad-hoc 双摘要签名的应用。
- 同名 `.ipa`：标准 `Payload/<名称>.app` ZIP 包。
- 同名 `.tar.gz`：供越狱设备系统目录安装使用。
- `app-manifest.json`：记录 Bundle ID、构建输入和签名后可执行文件哈希。

本轮 Meridian 设备的安装流程：将两个 `.tar.gz` 和 `install-uikit-device.sh` 放在同一文件夹，在 Filza 的 root 终端执行 `/bin/sh install-uikit-device.sh`。脚本只更新本示例的两个 `/Applications/NativeAOTUIKit*.app`，核对现有进程的完整路径后停止对应测试应用，并调用设备已有的 `uicache` 注册桌面图标。再次安装要求脚本自己的所有权记录存在，避免覆盖同名的其他应用。`--check` 可仅检查归档路径和安装位置。只使用本打包入口生成的归档。

安装后从桌面打开 **AOT 32** 或 **AOT 64**。设备已有 `uiopen` 时，也可用 `uiopen nativeaot-uikit32://` / `uiopen nativeaot-uikit64://` 打开本示例。首次运行的两个自动计数来自真正的 `UIControl` action 分发；日志将其标记为自动检查，和用户手动点击分开记录。

这台设备把系统目录应用的 Home 设为 `/var/mobile`，所以本例分别把状态、事件日志和窗口 PNG 保存到 `Documents/NativeAOTUIKit32` / `NativeAOTUIKit64`。应用通过 `NSHomeDirectory` 获取实际 Home，不把这个设备路径写死在 C# 中。窗口快照只是本例自己的窗口；连续点击时合并截图请求，避免阻塞交互。

自行打包其他应用时，先让项目调用 `UIApplicationMain` 并完成其 UI 初始化，再运行：

```bash
python3 ports/ios-legacy/publish-project.py /路径/MyApp.csproj --arch arm64 --link-framework UIKit --output /专用构建目录
python3 ports/ios-legacy/package-app.py /专用构建目录/publish --bundle-id org.example.myapp --display-name MyApp --app-name MyApp --output /专用打包目录 --sign
```

打包器不会把普通控制台 Main 自动改写为 UIKit 入口。`--url-scheme` 可选，应用自身需实现相应的 URL 处理。这里的签名和安装流程用于已越狱设备；未验证普通设备开发者签名、App Store、iPhone 布局或真实 iOS 7。ARM32 的 `CGFloat` 是 `float`，CGRect 返回使用 `objc_msgSend_stret`；ARM64 使用 `double` 和普通 `objc_msgSend`。所有消息发送声明必须匹配实际方法签名。

## 真机验证

最终确认 iOS 7 的最低兼容性，需要对应架构的真实 iOS 7 设备。iOS 10 的 iPad mini 4 可以验证 ARM32/ARM64 上的启动、GC、异常、线程、回调、文件路径和 UIKit 应用，但不能替代 iOS 7 测试。

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

## 大型 ARM32 应用与资源程序集

完整游戏会超过小探针未触及的 Mach-O / Thumb 限制。ARM32 对象写出按完整节点分段，使带 scattered relocation 的输入节保持在 24 位偏移范围内；链接器再合并同名节。托管方法放在只读可执行的 `__AOT,__managedcode`，通过 `-segprot __AOT rx rx` 链接，使系统导入桩与原生代码保持接近。

编译器生成的辅助跳转和外部函数桩使用不受 ±16 MiB 限制的跳转序列，保留 `r12` 隐藏参数及运行时 8 字节 red zone。导入函数地址放在可重定位的 `__DATA`，避免 dyld 在只读代码段执行绑定。原生运行时进入托管初始化、终结器、异常和堆栈跟踪的入口也使用间接调用。不应给整个 ARM32 构建添加 `-mlong-calls`：编译器隐式 libc 调用可能产生旧 dyld 不接受的只读段绑定。

`publish-project.py` 支持卫星资源程序集；`--strip` 在签名前去掉本地符号，并在构建目录保留 `.unstripped`。静态审核使用 `--symbols-binary` 检查与最终文件 UUID 相同的符号副本，同时拒绝 `__TEXT` / `__AOT` 的 dyld 绑定。`package-app.py` 支持 `--orientation landscape`、`--hide-status-bar` 和 `--version`。

2026-09-09 的完整游戏 ARM32 对象约含 24.8 MiB 托管代码，已通过链接及静态审核。新的跳转布局在 iOS 10.0.2 上通过 8 组运行时探针；游戏客户端也分别在 ARM32 / ARM64 完成进入关卡和音乐的人工验证。探针不代替游戏功能测试，未覆盖的模式与系统版本仍需继续验证。相关验证记录见 [2026-09-09](results/2026-09-09.md)。
