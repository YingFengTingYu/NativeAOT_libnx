# 第二十七轮：IPv4 网络与异步 Socket

本轮在独立运行时仓库实现公开 libnx Socket PAL，没有修改游戏源码。`System.Native` 复用上游地址、DNS、TCP/UDP、选项和错误转换，新增 `pal_libnx_networking.c` 处理 libnx 的非阻塞事件与 ABI 差异。没有链接 SDL、Linux socket 库或 OpenSSL TLS 后端。

## 实现范围

- 公开 `socket`、`bind`、`listen`、`accept`、`connect`、`send/recv`、`sendto/recvfrom`、`poll`、`getaddrinfo`、`getsockopt/setsockopt`；Socket 的 SafeHandle 就是 libnx/newlib 文件描述符。
- 当前 Socket 能力明确限制为 IPv4，IPv6/Unix socket 返回 `EAFNOSUPPORT`，避免 BCL 因平台含糊错误错误启用 IPv6 双栈。
- libnx 没有 epoll/kqueue。保留 .NET Unix SocketAsyncEngine，使用 poll 检查注册方向；通知后停止重复通知该方向，非阻塞操作返回 `EAGAIN` 时重新布防。空闲循环等待 2 ms，关闭描述符与 poll 共用锁，防止关闭后复用同号描述符污染注册。
- 不对 accept 执行 libnx 不支持的 `F_SETFD/FD_CLOEXEC`。非阻塞 connect 的 `EWOULDBLOCK` 规范为 BCL 所需的 `EINPROGRESS`。`SO_ERROR` 内的 Horizon/Linux errno 单独转为 PAL，不能当成 Newlib errno。
- 地址读回使用 0x100 字节中间缓冲，再按实际 IPv4/IPv6 长度复制。Eden v0.2.1 把 16 字节 IPv4 sockaddr 的返回长度报告为 0x100，直接交给 BCL 会使 LocalEndPoint 抛越界异常。没有修改模拟器。
- 普通单缓冲且没有辅助数据的消息通过 sendto/recvfrom，保留数据和来源地址。需要辅助数据或多个缓冲时使用公开 libnx sendmsg/recvmsg；系统版本不支持或 IPC 响应头无效时明确返回 `ENOTSUP`，不伪造 PacketInformation。
- `SystemNative_GetNetworkInterfaces` 返回 nifm 的真实当前 IPv4 地址/掩码视图，名字 `nifm-default`、Index 0 表示默认接口，没有捏造 BSD index、MAC 或连接速度。这不是完整系统网卡表。

宿主负责初始化 `socketInitialize`，不由 BCL 偷偷管理平台生命周期。退出时先销毁托管 Socket/HTTP 客户端，再调用：

```c
void SystemNative_LibnxQuiesceSocketEvents(void);
```

它在同一事件锁内停用后续 poll，让没有退出协议的 BCL 后台事件线程停在原生等待中，随后宿主可执行 `socketExit`。不会返回空成功事件或错误触发 SocketAsyncEngine 的 FailFast。该接口只供进程最终退出，不支持同一进程重启网络。

游戏需要真实局域网地址时，可通过独立的简化接口避开 Linux BCL 文件假设：

```c
int32_t SystemNative_LibnxGetCurrentIpv4Config(uint32_t* address, uint32_t* mask);
```

返回 PAL error，0 为成功；输出变量的四个内存字节为网络地址字节。通过公开 `nifmInitialize`、`nifmGetCurrentIpConfigInfo`、`nifmExit` 读取，失败保留零输出并报告 `ENETDOWN`。

## 实际验证

固定 Eden v0.2.1 CLI，SHA-256 为 `1FC704C297AA80F52EF3776C1B07077616B414C316FA0A7F2E538B32CCBB876A`。每次结果和日志由 `run-tls-probe.ps1` 写入对应 `history`，不提交产物。

最终原生探针 NRO SHA-256：`B2124E70DCA6D69EC87952FDA2A7C5D42DD822052FCD619CD7EFEB05F483B93D`；托管探针：`92B933194FFD83957A92ED90BB4EE2034AC268A9C519CB14989F5997E19A36BD`。两者均退出 0，通过项与未验证能力分别写入日志和结果 JSON。

原生探针：DNS `example.com`、TCP loopback、32 次 poll 读方向重新布防、句柄关闭、UDP 消息往返通过。nifm 返回本机真实活动虚拟网卡 `198.18.0.1/30`。辅助数据失败被规范成 `Error_ENOTSUP=65597`。SO_ERROR 的超时、进行中、拒绝和未知值映射检查通过。

托管探针已验证：

- `Dns.GetHostAddressesAsync`、TCP accept/connect 和 64 次异步读写。
- 取消挂起读取、取消后继续接收、Dispose 中断挂起 UDP 接收。
- 16 次 `UdpClient.ReceiveAsync/SendAsync` 单播往返。
- `ReceiveMessageFromAsync` 的辅助数据不支持错误被正确捕获，随后 `ReceiveFromAsync` 保留并读出此前排队的完整 UDP 数据，没有伪造目的地址。
- `SocketsHttpHandler` 真实 HTTP GET 和完整响应。
- `ClientWebSocket` 的 ws 握手、中文文本帧往返及正常关闭握手。
- 原生网卡视图与真实地址、连接拒绝错误传播、退出 quiesce 后 socketExit。

既有 `System` 探针重新构建并在模拟器通过，包括 SD 文件操作、位置保留、向量 I/O、RomFS 读取/只读和路径往返，确认文件关闭接入没有破坏原测试。

## 明确的限制

- **stock `NetworkInterface.GetAllNetworkInterfaces` 仍不能直接使用。** Linux BCL 在调用 PAL 前读取 `/etc/resolv.conf`，缺目录时抛 `DirectoryNotFoundException`；它只捕获 FileNotFoundException。没有伪造 SD 上的 `/etc`。游戏需通过上述平台 IPv4 接口提供地址列表。
- **Eden 组播没有通过。** 源码 `BSD::SetSockOptImpl` 对所有非 SOL_SOCKET 的设置只返回 stub 成功，包括加入组播和选择组播接口；实际发送探针返回网络不可达。测试结果 JSON 单独保存 `MulticastVerified=false`，基本网络通过不表示局域网自动发现已经验收。
- **Eden 辅助数据不可用。** BSD `SendMMsg/RecvMMsg` 命令未实现，libnx 对无效响应报告 `LibnxError_InvalidCmifOutHeader`。调用方应回退 ReceiveFromAsync，目标地址信息保持缺省，按真实本地地址/子网选择响应。
- TCP_NODELAY 等部分模拟器 Socket 选项同样是 stub，不能用调用返回成功证明它们的实际效果。真机选项、组播和辅助数据仍待验证。
- 这轮没有接入运行时 SslStream/OpenSSL，HTTPS/WSS 由游戏宿主的公开 libnx ssl Stream 适配验证；本记录只证明 HTTP/ws。
- IPv6、完整 BCL 网卡信息、真机网络切换/休眠恢复、真实跨机器 LAN、长期稳定性和隐式硬件异常仍未验证。

## 重现入口

先按 README 构建 native runtime 与修改过的 ARM64 host JIT。托管探针额外需要 Linux x64 .NET SDK 10.0.400，可构建 `Dockerfile.managed`：

```sh
docker build -t nativeaot-libnx-managed:10.0.11 -f ports/libnx/Dockerfile.managed ports/libnx
docker run --rm -v "$PWD:/runtime" nativeaot-libnx-managed:10.0.11 bash ports/libnx/build-network-probe.sh
docker run --rm -v "$PWD:/runtime" nativeaot-libnx-managed:10.0.11 bash ports/libnx/build-managed-network-probe.sh
```

```powershell
.\ports\libnx\run-tls-probe.ps1 -EdenPath '模拟器绝对路径/eden-cli.exe' -Suite Network
.\ports\libnx\run-tls-probe.ps1 -EdenPath '模拟器绝对路径/eden-cli.exe' -Suite NetworkManaged
```

本轮先使用游戏已经缓存的同一 devkitPro 镜像和 .NET SDK 10.0.400 镜像，随后构建上面的独立托管探针镜像。构建脚本和测试源码没有依赖游戏目录或本地绝对路径。
