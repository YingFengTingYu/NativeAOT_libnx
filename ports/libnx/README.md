# NativeAOT / libnx 移植实验

本分支基于公开的 `dotnet/runtime`，目标是直接使用 devkitPro/libnx 构建 Nintendo Switch homebrew。当前处于源码构建与 ABI 验证阶段，尚未实现可运行的 NativeAOT/libnx 运行时。

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
