# 2026-09-09：普通项目发布与互操作扩展

本轮完成普通 `.csproj` 发布入口，并新增结构体互操作测试。**本机验证已通过，本轮最终产物尚未取得 iPad 真机运行结果。** 此前七组 iOS 10 真机结果见 [ARM32 运行时记录](2026-09-09-arm32.md)，不能用于代替本轮验证。iOS 7 真机仍未验证。

## 普通项目发布

`publish-project.py` 接受普通 `net10.0`、`OutputType=Exe` 项目，通过 SDK 构建原项目图，再使用对应架构的源码基础库、本地 ILC、pthread TLS 运行时和 iOS 9.3 SDK 生成 iOS 7 命令行程序。

实际将 `examples` 复制到仓库外、名称含空格的临时路径，在 ARMv7 和 ARM64 上均完成：

- 默认 C# 源文件选择、项目引用和传递的 `System.IO.Hashing` NuGet 依赖。
- `LibraryImport` 源生成。
- 嵌入资源和普通内容文件随输出发布。
- 原生链接、ad-hoc SHA-1/SHA-256 签名及签名校验。
- 架构、最低版本、动态依赖、TLS，以及 ARM32 私有展开表和运行时全局符号静态审计。

对库项目、不支持的目标框架和非专用输出目录的拒绝检查通过。每次构建保留独立工作目录、命令、日志、审计和包含二进制哈希的清单；失败不会覆盖前一次成功发布。

## 新增互操作测试

第 8 组 `interop` 检查：

- 11 项结构体大小/字段偏移，直接与同工具链编译的 C 函数对照。
- 1 字节结构体、8/12 字节结构体、寄存器与栈之间拆分的结构体参数。
- 四个 `float` 和两个 `double` 的同类型浮点聚合传参/返回。
- 整数与浮点混合结构体、`Pack=1`、嵌套结构体、`ref/out`。
- 12 个交替 `float`/`double` 参数、负零、NaN 和无穷大。
- 委托及 `UnmanagedCallersOnly` 结构体回调，共 32 次，其中 16 次由外部 pthread 调用；回调内强制压缩 GC 并再次 P/Invoke，检查托管对象保活、闭包、参数和返回值。

回调内部捕获托管异常并返回失败标记，不让异常跨越原生调用方。

## ARM32 默认结构体对齐修复

Clang 对 ARMv7 iOS 的 `{ int32_t; double; int16_t; }` 给出 16 字节大小，`double` 偏移为 4。类型系统原先沿用其他 ARM 平台的 8 字节默认结构体对齐。

新增类型系统回归在修复前准确失败：ARM/iOS 期望对齐 4，实际为 8；ARM/Linux 和 ARM64/iOS 两个对照用例通过。修复仅调整 Apple ARM32 未指定 `Pack` 时的结构体默认规则；显式 `Pack=1`、`Pack=8` 和托管 `Int64` 的基本对齐均有回归检查。

修复后，**87 项 FieldLayout 测试全部通过（0 跳过）**；本地 iOS ILC 重建通过，0 警告、0 错误。Apple Silicon Mac 使用更新后的编译器完成八组运行测试，输出 `PASS ALL (8 suites)`，退出码为 0。

## 本地复现

```bash
./build.sh clr.tools -c Release -os ios -arch arm64 --cross
.dotnet/dotnet test src/coreclr/tools/aot/ILCompiler.TypeSystem.Tests/ILCompiler.TypeSystem.Tests.csproj -c Release -p:TargetOS=osx -p:TargetArchitecture=arm64 --filter FullyQualifiedName~FieldLayout
python3 ports/ios-legacy/publish-project.py ports/ios-legacy/tests/managed/LegacyIOSProbe.csproj --arch arm --native-source ports/ios-legacy/tests/managed/callback.c --sign
python3 ports/ios-legacy/publish-project.py ports/ios-legacy/tests/managed/LegacyIOSProbe.csproj --arch arm64 --native-source ports/ios-legacy/tests/managed/callback.c --sign
python3 ports/ios-legacy/build-probe.py --platform osx
artifacts/legacy-ios/probes/osx/publish/LegacyIOSProbe
```

普通项目示例的命令和参数见 [README](../README.md#普通-csproj-发布入口)。

## 待运行的真机产物

最终四个产物已经准备并通过静态检查。测试会话使用同一 iPad 上的临时 `device-session-v8.sh`；结果须读取真实日志及退出码后再确认。最终测试任务为 `device-job-034`，四个程序都通过后会自动结束会话。早期任务 032/033 保留为本轮初版对照。

| 程序 | SHA-256 |
|---|---|
| `ProjectHello-arm` | `a36b7d6bc9be289674ec67264db70457b6a5d3c9ff5e273e045eecd81579aa64` |
| `InteropProbe-arm` | `c385c805f328d88becd453437eb51ec933ae57319f4a09a2676a8c7a13d03983` |
| `ProjectHello-arm64` | `9ec65f914945b26f33ebac0f171c69ff138067ed0ec55cce11362fe87ac7024b` |
| `InteropProbe-arm64` | `09b1eda91743e6b69d1d0ffa3f66ed1d8234db8b04a1de7233a99496b6fbe713` |

本地证据：

- `artifacts/log/legacy-ios/interop-layout-tests-before.log`：修复前 1 失败、2 通过。
- `artifacts/log/legacy-ios/interop-layout-tests-after.log`：修复后 87 通过。
- `artifacts/log/legacy-ios/interop-compiler-build.log`：本地编译器重建。
- `artifacts/log/legacy-ios/interop-macos-final-build.log`、`interop-macos-final-run.log`：Mac 八组回归。
- `artifacts/log/legacy-ios/interop-native-layout.log`：Clang ARMv7 布局。
- `artifacts/log/legacy-ios/project-negative-tests.json`：入口拒绝检查。
- `artifacts/legacy-ios/project-interop-device-inputs.json`：四个产物的清单和构建日志目录。

当前没有完整 runtime-pack 或 `.app` 打包；含 RID 专属资产的 NuGet 包、卫星资源程序集和原生共享库输出未接入。复杂互操作新增用例需要 iPad 真机结果，Objective-C 异常跨边界、完整加密、TLS/HTTP 和非 invariant 全球化仍未验收。
