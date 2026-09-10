# 第二十八轮：可选的原生读取中转缓冲

游戏真机与 Windows 的同资源对照显示读取阶段存在明显差距，但两者硬件、存储与运行时不同，不能单凭差距认定是模拟 pread 的定位调用所致。libnx 的 RomFS/fsdev 在 IPC 缓冲区不适用时会回退到 4 KiB 栈缓冲；此行为是否在游戏真机中大量出现，需要额外计数验证。

新增宿主可选接口 `SystemNative_LibnxSetReadBuffering`，默认关闭。启用后，PRead 及 PReadV 的 4 KiB 至 128 KiB 读取使用一个 4 KiB 对齐、128 KiB 大小的原生堆缓冲，完成 read 后只复制实际返回的字节。缓冲区在原有位置锁内复用，随进程释放；不缓存文件内容、不改变写入路径。更小或更大的读取、零长度读取及额外缓冲分配失败均保留原路径。PRead 的定位与恢复语义不变。

`SystemNative_LibnxGetBufferedReadCount` 返回实际进入中转路径的累计次数，供宿主报告确认开关确实生效。该优化没有替换 stdio 描述符体系，没有读取 libnx 私有结构，也没有修改第三方 libnx。

已执行 `build-runtime.sh`、`build-system-probe.sh` 和固定 Eden CLI 的 System 探针：通过。覆盖内容包括：

- 默认/启用两种路径读取相同 SD 内容，结果逐字节一致；4 KiB/128 KiB 边界内外、零长度、小读取、非对齐目标和非对齐文件偏移。
- 定位保持、PReadV、文件尾短读和 EOF、非法负偏移。
- 原位写入后立刻重新读取，确认没有内容缓存导致的陈旧数据。
- 两个线程对同一个描述符各读取 64 次，数据及原始文件位置正确。
- RomFS 内嵌 256 KiB 文件的大块读取、逐字节验证、尾部短读、位置保持及只读属性。
- 既有内存、监视器、时间、随机数和 SD 文件回归。

`file.buffered_control=1`、`file.buffered_staging=1`、`file.buffered_concurrent=1`、`romfs.buffered_large=1`、`pass=1`，进程退出 0。结果在 `artifacts/libnx/system-probe/history`。

尚未验证额外分配失败的故障注入、Switch 真机 IPC 回退次数及性能收益。不能用模拟器吞吐量代替真机测量。宿主需要保留同版本开关对照；取得真机证据前不默认启用。
