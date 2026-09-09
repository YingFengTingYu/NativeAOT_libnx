# libnx 的 OpenSSL / SslStream 后端

这是运行时内的可选加密后端。它复用上游 `System.Security.Cryptography.Native` 的 TLS、X.509、摘要和非对称加密接口，配合现有 .NET Unix 托管程序集；应用使用标准 `SslStream`，不需要自定义 TLS Stream、HTTP scheme 改写或游戏平台接口。

OpenSSL 的 TLS 记录通过 memory BIO 进出，网络 I/O 仍由 `SslStream.InnerStream` 执行。公开 libnx SSL 服务绑定 socket，不能直接用于任意托管流；因此本后端使用 OpenSSL 软件 TLS，Socket 和安全熵源仍由公开 libnx 提供。

## 构建

先按上级 [README](../README.md) 准备固定容器和 ARM64 ILC 代码生成器。在运行时仓库根目录执行：

```sh
docker run --rm -v "$PWD:/runtime" -e LIBNX_USE_OPENSSL=1 nativeaot-libnx-managed:10.0.11 bash ports/libnx/build-runtime.sh
docker run --rm -v "$PWD:/runtime" nativeaot-libnx-managed:10.0.11 bash ports/libnx/build-sslstream-probe.sh
```

`openssl.lock.json` 固定官方 OpenSSL 3.5.8 发布源码和 SHA-256。`build-openssl.sh` 校验下载，应用本目录补丁，在容器临时目录生成源码和编译中间文件，将库、头文件、许可证及构建日志写入 `artifacts/libnx/openssl`。构建缓存核对输入指纹和静态库摘要。

默认仍是原有的小型 mbedTLS 摘要适配。使用完整后端时，原生构建、`generate-interop-list.sh`、ILC 和最终链接必须使用同一套 `LIBNX_USE_OPENSSL=1` 产物。不能混用两个后端中同名的 `CryptoNative_*` 符号。

## 平台差异

- `csrng.patch`：OpenSSL 的熵源使用 `csrngInitialize/csrngGetRandomBytes/csrngExit`，失败返回错误并清理输出，不使用时钟或非密码学随机数补足。
- `platform.patch`：libnx 没有 POSIX setuid 执行模式，也没有当前 OpenSSL 路径所需的 `posix_memalign`；分别使用无 setuid 平台分支和 OpenSSL 自有的对齐分配路径。不伪造通用 UID API。
- 不启用 syslog、动态模块、引擎、OpenSSL socket BIO、QUIC 或汇编优化。保留 pthread 同步与线程局部状态。此配置针对 .NET memory BIO 路径，不是通用 OpenSSL 命令行发行包。
- 证书、用户证书存储和 CRL 的原生文件访问复用 `System.Native` 的 Unix 路径映射：`/romfs` 对应 `romfs:/`，其他绝对路径对应 SD。非 libnx 原生构建仍调用原来的函数。
- newlib 没有 `timegm`，OCSP 缓存到期时间使用 OpenSSL UTC 日历差值计算，避免修改进程全局时区。

## 信任库与验证

默认 OpenSSL 目录为 `/dotnet/ssl`，可通过标准 `SSL_CERT_FILE` 和 `SSL_CERT_DIR` 环境变量配置。变量使用托管 Unix 路径；宿主可以将 CA 包放在 RomFS。缺少信任库必须导致证书不受信任，不能改为跳过校验。

运行时在未提供 `HOME` 时使用 `/dotnet` 作为托管持久化目录，供 BCL 的用户证书存储和缓存使用；这不是模拟 Unix 用户或 passwd 数据库。宿主可在运行时启动前设置 `HOME`，将数据限定到自己的应用目录；运行时不覆盖已有值。

探针下载 `ca-bundle.lock.json` 锁定的 Mozilla/curl CA 包，保留 PEM 中的来源和 MPL-2.0 声明。模拟器脚本将其部署到本运行时仓库的独立 SD，不改系统或玩家的信任存储：

```powershell
.\ports\libnx\run-tls-probe.ps1 -EdenPath '模拟器绝对路径/eden-cli.exe' -Suite SslStream
```

宿主对照可直接运行：

```sh
dotnet run --project ports/libnx/tests/sslstream/TlsProbe.csproj -c Release -p:UseArtifactsOutput=true -p:ArtifactsPath=artifacts/libnx/sslstream-host
```

探针覆盖临时证书创建与 PKCS#12、TLS 1.2/1.3、ALPN、不依赖 socket 的分段流、读取取消后继续使用、`close_notify`、主机名/信任错误、握手取消、本地 HTTPS 和 WSS 二进制回显。目标端另验证默认信任库的公共 HTTPS，以及自签名、主机名不符、过期证书拒绝。主机通过不代表目标端通过；每轮实际结果记录在 `ports/libnx/results`。

尚未据此验收客户端证书、撤销检查、OCSP stapling、会话恢复、密钥更新、长时间并发、真机或完整密码库 API。
