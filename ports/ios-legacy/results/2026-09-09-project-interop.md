# 2026-09-09：普通项目发布与互操作真机验证通过

普通 `.csproj` 发布入口和扩展后的八组测试，在 **iPad mini 4 / iOS 10.0.2（14A456）** 上完成 ARMv7、ARM64 两种架构验证。最终任务 038 于 2026-09-09 08:00（UTC+8）执行，四个程序全部退出 0。**尚未在真实 iOS 7 设备上验证最低系统兼容性。**

| 产物 | ARMv7 | ARM64 |
|---|---|---|
| 普通项目示例 | 通过，4 字节指针，退出 0 | 通过，8 字节指针，退出 0 |
| 八组运行时与互操作测试 | `PASS ALL (8 suites)`，退出 0 | `PASS ALL (8 suites)`，退出 0 |

## 普通项目发布

`publish-project.py` 接受普通 `net10.0`、`OutputType=Exe` 项目，通过 SDK 构建原项目图，再使用对应架构的源码基础库、本地 ILC、pthread TLS 运行时和 iOS 9.3 SDK 生成 iOS 7 命令行程序。

实际将 `examples` 复制到仓库外、名称含空格的临时路径。以下功能在两种架构的真机程序中均通过：

- 默认 C# 源文件选择、项目引用和传递的 `System.IO.Hashing` NuGet 依赖。
- `LibraryImport` 源生成调用 `getpid`。
- 嵌入资源读取、普通内容文件随输出发布并在设备上读取。

两种架构均完成原生链接、ad-hoc SHA-1/SHA-256 签名及校验，并通过架构、最低版本、依赖、TLS 和 ARM32 展开表的静态审计。库项目、不支持的目标框架和非专用输出目录的拒绝检查通过。每次构建保留独立工作目录、命令、日志、审计和哈希清单；失败不会覆盖前一次成功发布。

## 新增互操作测试

第 8 组 `interop` 在两种架构的真机上检查：

- 11 项结构体大小/字段偏移，直接与同工具链编译的 C 函数对照。
- 1 字节结构体、8/12 字节结构体、寄存器与栈之间拆分的结构体参数。
- 四个 `float` 和两个 `double` 的同类型浮点聚合传参/返回。
- 整数与浮点混合结构体、`Pack=1`、嵌套结构体、`ref/out`。
- 12 个交替 `float`/`double` 参数、负零、NaN 和无穷大。
- 委托及 `UnmanagedCallersOnly` 结构体回调，共 32 次，其中 16 次由外部 pthread 调用；回调内强制压缩 GC 并再次 P/Invoke，检查对象保活、闭包、参数和返回值。
- 16 个包含嵌套结构体的托管对象：压缩 GC 前后检查 64 位原子变量对齐，并执行 `Volatile.Read`、`Interlocked.CompareExchange` 和 `Interlocked.Read`。

回调内部捕获托管异常并返回失败标记，不让异常跨越原生调用方。原有 clock、GC、异常、线程、基础回调、Task 和文件测试也全部通过。

## 保留托管对齐，转换原生布局

ARMv7 iOS 的 C 结构体 `{ int32_t; double; int16_t; }` 大小为 16 字节，`double` 偏移为 4；普通 .NET 托管结构体大小为 24 字节、偏移为 8。初版真机测试明确报告了这一差异。

随后整体修改默认结构体对齐虽然让互操作组通过，却使线程池内部的 64 位计数器失去 8 字节对齐，触发 `RhpLockCmpXchg64` 的 `EXC_ARM_DA_ALIGN`。单独运行 Task 也能复现，证明问题不是前一个回调破坏内存。

最终实现：

1. 恢复通用托管布局，保持线程池和其他原子变量的对齐要求。
2. 仅让 `NativeStructType` 的 Apple ARM32 默认原生表示采用 4 字节打包。
3. 对布局不同的结构体，`MarshalUtils` 不再判定其可直接按原字节传递，而是使用已有的字段封送。`DllImport`、委托回调和 `ref/out` 的转换在真机验证通过。
4. `UnmanagedCallersOnly` 不执行封送，其接口类型明确选择原生布局：ARM32 使用 `Pack=4`，ARM64 保留相应原生布局。通用入口提供 `LegacyIOSArchitecture` 和 `LegacyIOSMinimumOSVersion`，供项目设置编译条件。

相关规则参见 [Apple ARM 调用约定](https://developer.apple.com/documentation/xcode/writing-armv6-code-for-ios) 和 [.NET 内存模型](../../../docs/design/specs/Memory-model.md)。显式打包的原生接口类型不应替代所有托管内部数据结构。

最终 **126 项布局与封送测试全部通过（0 跳过）**；本地 iOS ILC 重建通过，0 警告、0 错误。Apple Silicon Mac 的八组回归也通过。旧 `build-arm32-probe.py` 入口仍可构建和通过静态审计。

## 本地复现

```bash
./build.sh clr.tools -c Release -os ios -arch arm64 --cross
.dotnet/dotnet test src/coreclr/tools/aot/ILCompiler.TypeSystem.Tests/ILCompiler.TypeSystem.Tests.csproj -c Release -p:TargetOS=osx -p:TargetArchitecture=arm64 --filter 'FullyQualifiedName~FieldLayout|FullyQualifiedName~MarshalUtils'
python3 ports/ios-legacy/publish-project.py ports/ios-legacy/tests/managed/LegacyIOSProbe.csproj --arch arm --native-source ports/ios-legacy/tests/managed/callback.c --sign
python3 ports/ios-legacy/publish-project.py ports/ios-legacy/tests/managed/LegacyIOSProbe.csproj --arch arm64 --native-source ports/ios-legacy/tests/managed/callback.c --sign
python3 ports/ios-legacy/build-probe.py --platform osx
artifacts/legacy-ios/probes/osx/publish/LegacyIOSProbe
```

普通项目示例的命令和参数见 [README](../README.md#普通-csproj-发布入口)。设备执行继续采用此前的系统目录方案，未安装常驻测试服务。

## 最终产物与证据

最终程序保留在设备的 `/usr/local/libexec/NativeAOTProjectTests/arm/` 与 `arm64/` 目录，分别为 `Hello` 和 `InteropProbe`。四个已安装文件经 USB 读回后，与本地最终产物逐一核对 SHA-256，全部一致。清理任务 039 退出 0；临时会话 `v8` 已停止，旧的系统目录测试变体已清理。

| 程序 | SHA-256 |
|---|---|
| `ProjectHello-arm` | `e22aeb69c417a59247101edac7f1bb6315b635508300206fd7f7f06eac54b045` |
| `InteropProbe-arm` | `1f58e9922a7abecef82297cc38c520bb073adb00201308a3b159250703954093` |
| `ProjectHello-arm64` | `9c0f941a19e0be17d581bb9a7a96d7701e8b401f2b53f7ef87d93fac4a0e07e9` |
| `InteropProbe-arm64` | `8260fad44abfda6f9a663d75fbcdc084133395462776f2b2623c4f3b8b14de80` |

本地证据：

- `artifacts/log/legacy-ios/ipad-device-job-038.log`、`.done`：最终四程序矩阵通过。
- `artifacts/log/legacy-ios/ipad-device-job-036.log`：ARM32 修复后首次八组通过。
- `artifacts/log/legacy-ios/ipad-device-job-033.log`：初版结构体布局差异。
- `artifacts/log/legacy-ios/ipad-device-job-034.log`、`ipad-device-job-035.log`：整体降低对齐导致的原子操作崩溃。
- `artifacts/log/legacy-ios/interop-layout-marshalling-tests.log`：126 项回归。
- `artifacts/log/legacy-ios/interop-compiler-marshalling-build.log`：编译器重建。
- `artifacts/log/legacy-ios/interop-marshalling-macos-build.log`、`interop-marshalling-macos-run.log`：Mac 八组回归。
- `artifacts/log/legacy-ios/project-negative-tests.json`：入口拒绝检查。
- `artifacts/legacy-ios/project-interop-device-final-inputs.json`：四个产物的清单和工作目录。
- `artifacts/legacy-ios/project-interop-device-results.json`：真机状态、退出码和已安装文件的读回校验。
- `artifacts/log/legacy-ios/ipad-device-job-039.log`、`ipad-device-session-v8.status`：安装整理与会话结束。

当前没有完整 runtime-pack 或 `.app` 打包；含 RID 专属资产的 NuGet 包、卫星资源程序集和原生共享库输出未接入。覆盖的互操作用例通过不代表所有 ABI 情形均已验收；Objective-C 异常跨边界、完整加密、TLS/HTTP、非 invariant 全球化以及 iOS 7 真机仍待验证。
