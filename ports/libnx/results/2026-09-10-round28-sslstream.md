# 第二十八轮：运行时 SslStream / OpenSSL 适配

本轮从 `4b4da26c404` 开始，目标是让标准 `SslStream`、`HttpClient` 和 `ClientWebSocket` 在 libnx 运行时工作，收回此前游戏自有 TLS 传输的职责。实现与探针均位于独立运行时仓库。

## 第一阶段：后端构建与宿主对照

新增可选 `LIBNX_USE_OPENSSL=1`，交叉编译固定 OpenSSL 3.5.8（官方源码 SHA-256 `a8f84a39918ec6415ce765d9b429d313ba97b8143169c172e734b9514464f5b2`），复用上游 crypto/X.509/SSL 原生适配层。保留 .NET Unix 托管 TLS 实现及任意 InnerStream 的 memory BIO 路径，没有在游戏 Core 添加补救接口，也没有绑定 libnx SSL socket 服务来冒充任意流。

平台补丁接入 libnx CSRNG，关闭不适用的 syslog/setuid 检查，使用 OpenSSL 自有对齐分配路径。证书原生文件访问共享现有 SD/RomFS 映射；OCSP 时间转换在 libnx 使用 OpenSSL UTC 日历运算，其他平台保持原调用。具体配置与重现命令见 [OpenSSL 后端说明](../openssl/README.md)。

实际通过：

- devkitA64 GCC 15.2.0 / newlib / libnx 编译 OpenSSL 静态库。
- `.NET System.Security.Cryptography.Native.OpenSsl` 的全部 35 项原生编译/归档步骤通过。功能链接探测中 EC2M、ALPN、ChaCha20-Poly1305、SHA3、DigestSqueeze、消息签名入口通过；Engine 按配置禁用。
- 修改后的 crypto PAL 在 Linux x64 宿主独立编译通过。
- Windows 与 Linux .NET 10 宿主探针通过：ECDSA 临时证书/PKCS#12、TLS 1.2/1.3、ALPN、65537 字节任意流分段传输、读取取消后继续使用、close_notify、leaveInnerStreamOpen、主机名/信任错误回调并拒绝、握手取消、本地 HTTPS 和 WSS 二进制往返/关闭。

编译产物摘要：

- libssl.a：`468f139b9e3253a13a07d563666c81bdc360a5d5ef7c052ecbce45d72e5cc1e4`
- libcrypto.a：`13b03dd7aa4a19699ddb16473409a22e1ab8635c4838bcbc1337404f16247f95`
- crypto PAL：`7a0bc2072aaee3991eeedb5a548c75dda7e74334435b82ec63817df021c11b5d`

失败与修正记录：初始 OpenSSL 构建缺少 syslog.h；初始功能链接探测暴露 newlib 缺少 posix_memalign 和 POSIX UID 函数；初始 PAL 编译缺少 timegm。均经上述目标限定的补丁处理后重新构建。Windows Schannel 无法直接使用本探针最初的临时 ECDSA 密钥，探针改为正常 PKCS#12 重导入，仍在 Dispose 时释放测试密钥；这属于宿主探针修正。

托管 ARM64 NRO 链接通过，摘要为 `4c15c7ba7daf339103ab884fddcccc8b573cb72e2fef1c97ffb20f1c2b92e331`。第一次模拟器运行通过 ECDSA 证书生成/PKCS#12，但 TLS 1.2 握手前打开用户证书存储时抛出 `The home directory of the current user could not be determined`，退出 1。libnx 没有 Unix passwd 数据库，下一阶段补充托管环境目录约定；本次不记为目标 TLS 通过。

原有 System 探针重新链接并在模拟器通过，覆盖随机数、SD 文件位置/截断/向量读写，以及 RomFS 读取、只读和路径映射。NRO 摘要 `b3ccdd7975f0ed7a8e502410ad96ccc50d51b6d916e8f826282291e9975c5383`。日志与结果分别保存在 `artifacts/libnx/sslstream/history`、`artifacts/libnx/system-probe/history`。

游戏仓库保持无代码改动，运行时默认仍采用之前的小型摘要后端。

## 第二阶段：目标端任意流 TLS

运行时初始化在未提供 `HOME` 时设为 `/dotnet`，作为 BCL 持久化数据目录；已有 HOME 不覆盖，没有伪造 passwd 信息。重新编译运行时并重链同一托管对象，NRO 摘要 `ebae77dbcfcc57b2460b69a4ad2a8c5596d68947c583b57f9e9f4fc64720848d`。

第二次模拟器运行通过证书生成、TLS 1.2/1.3 双端握手、ALPN、65537 字节任意流分段传输、读取取消后继续使用、正常 TLS 关闭、主机名/信任错误回调并拒绝、握手取消。这里的 InnerStream 是不含 socket 的双向 Channel 流，确认没有绕过 SslStream 的流语义。

本地 HTTPS 阶段失败，客户端内层异常为 `Received an unexpected EOF or 0 bytes from the transport stream`。后续 WSS、公共 HTTPS 和坏证书用例未执行，本次整体退出 1；不能记为 HTTP/WSS 通过。下一步补充本地 TLS 服务端异常记录定位失败来源。第一阶段实现提交为 `4b2ae81a36d`。

## 第三阶段：确认 Eden 的 MSG_PEEK 缺陷

服务端异常为 `Cannot determine the frame size or a corrupted frame was received`。进一步抓取首次非空读取，前缀为 `03 01 05 E4 ...`，TLS 握手记录应有的首字节 `16` 已被消费。该诊断版 NRO 摘要 `2c7a595f345abae4ab81373cfe54d59bc311e071d7666aaeab6a1afdcbec4c88`。

`SslStream.IO.cs` 在分配接收缓冲区前对 InnerStream 做零长度读取，Unix Socket 实现将其转换成 1 字节 `MSG_PEEK`。核对 [Eden v0.2.1 原始源码](https://git.eden-emu.dev/eden-emu/eden/src/tag/v0.2.1/src/core/internal_network/network.cpp)：`Socket::Recv` 和 `RecvFrom` 断言 flags 为 0，实际传给宿主 recv/recvfrom 的也是固定 0，导致 peek 变成消费数据。模拟器日志同时记录 `network.cpp:957 assert flags == 0`。

新增不链接任何 .NET 运行时的纯 libnx 最小复现：

```sh
docker run --rm -v "$PWD:/runtime" nativeaot-libnx-managed:10.0.11 bash ports/libnx/build-socket-peek-probe.sh
```

```powershell
.\ports\libnx\run-tls-probe.ps1 -EdenPath '模拟器绝对路径/eden-cli.exe' -Suite SocketPeek
```

探针发送 `16 03 01 04`，peek 返回 1 字节且值为 22，但随后的正常接收仅得到 3 字节，首字节为 3；预期是保留全部 4 字节且首字节仍为 22。`peek.preserves_data=0`，进程退出 1。该 NRO 摘要为 `814939681e13cc68b724b2dd5f198aa9583d9d3f97e00352fb1e5524e994cb14`，结果保存在 `artifacts/libnx/socketpeek-probe/history`。

因此当前无法用该 Eden 版本验收默认 NetworkStream 上的标准 HTTPS/WSS。没有把这项缺陷处理成游戏 Core 分支、修改 SslStream 跳过零长度读取，或伪造 Socket.Peek 的成功结果。游戏 TLS 路径和运行时锁定保持原状；任意流 TLS 的目标通过项仍成立。真机的 MSG_PEEK 与标准网络 TLS 尚待验收。
