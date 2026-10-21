# 第三十一轮：文件读取与 GC 映射内存对照

## 目的与结果

检查游戏通过托管缓冲区读取 RSB 时，是否因 GC 堆的映射方式触发 libnx 的 4 KiB 安全读取回退。当前没有复现该回退，因此没有修改 `System.Native` 文件实现，也没有加入未经证实的中转缓冲区优化。

`LibnxMemoryReserve/Commit` 使用 `svcMapMemory` 映射 GC 内存。探针必须调用这两个实际入口；`SystemNative_MMap` 当前是原生堆分配，并不代表 GC 堆。开发时的 `baseline-2` 只测到了后者，不能作为 GC 内存证据。

正式对照分别使用普通 `malloc` 和 GC 分配入口，缓冲区偏移 24 字节，连续读取 4 MiB 文件，每次 128 KiB。链接器包装 `fsFileRead`、`fsStorageRead`，直接统计服务入口调用数、请求不超过 4 KiB 的次数和 `0xD401` 结果。SD 和 NRO 内嵌 RomFS 分别检查数据内容，并验证定位读取没有改变原文件位置。

## 验证

构建使用本地 `lawn-switch-build:10.0.11` 镜像；目标代码使用 devkitA64 的 `-Wall -Wextra -Werror`。两个独立启动的头部无图形探针通过内容、位置、清理和正常退出检查。未运行 deko3d。

| 存储 / 缓冲区 | 请求数据 | 服务调用数 | ≤4 KiB 调用 | `0xD401` |
| --- | --- | --- | --- | --- |
| SD / 原生堆 | 4 MiB | 32 | 0 | 0 |
| SD / GC 映射 | 4 MiB | 32 | 0 | 0 |
| RomFS / 原生堆 | 4 MiB | 32 | 0 | 0 |
| RomFS / GC 映射 | 4 MiB | 32 | 0 | 0 |

`svcQueryMemory` 的公开类型分别为 `5` 和 `11`；直接将 GC 映射地址交给 `fsFileRead` 也成功。模拟器读取耗时约 2–3 ms，只用于确认探针正常运行，不代表实机 SD 性能。

- NRO SHA-256：`44ADC30E9CF8E7678E7B6010E18D7AE82B5DCAEB0EF72EE086249118CE8BC01E`。
- Eden v0.2.1 CLI SHA-256：`1FC704C297AA80F52EF3776C1B07077616B414C316FA0A7F2E538B32CCBB876A`。
- 原始结果：`artifacts/libnx/io-probe/gc-mapping-baseline-{1,2}/result.json` 和 `eden.log`；两次均 `Passed=true`。
- 首次 `baseline-1` 因重复挂载已由 libnx 挂载的 SD 提前退出，不计通过；探针已改为检查现有挂载。

## 源码交叉检查及边界

- [libnx fsdev](https://github.com/switchbrew/libnx/blob/master/nx/source/runtime/devices/fs_dev.c) 与 [RomFS](https://github.com/switchbrew/libnx/blob/master/nx/source/runtime/devices/romfs_dev.c) 确实存在收到 `0xD401` 后使用 4 KiB 栈缓冲区重试的路径，但不能仅凭存在此路径就认定游戏触发了它。
- [Atmosphère 内存状态定义](https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libmesosphere/include/mesosphere/kern_k_memory_block.hpp) 中，公开类型 `0x0B` 对应 Stack，包含普通、NonSecure、NonDevice IPC 许可。公开 `svcQueryMemory` 返回值会屏蔽内部能力位，不能用公开类型值直接按能力位测试。
- `PRead` 中保存位置、设置偏移、恢复位置的三个 `lseek` 主要更新 libnx 文件对象状态，不等于三次物理磁盘读取。

模拟器和源码均未支持“GC 映射必然导致碎片化读取”的假设，仍缺少实机探针及实际游戏分段日志。下一步应分离 FileStream 读取、解密、Zstd、行过滤与纹理提交耗时后再选择优化，不能用文件探针推导一秒停顿的主因。

## 重现

先按运行时构建说明生成当前 `libSystem.Native.a`，再执行：

```sh
bash ports/libnx/build-io-probe.sh
```

```powershell
.\ports\libnx\run-io-probe.ps1 -EdenPath '已有的 Eden v0.2.1/eden-cli.exe' -RunName gc-mapping-baseline-3
```

脚本使用独立 `artifacts/libnx/io-emulator` 便携目录，重新生成仅用于无图形探针的配置；结果目录已存在时拒绝覆盖。运行同一 NRO 的实机结果需单独记录，不能沿用模拟器的 `HardwareVerified=false` 报告宣称实机通过。
