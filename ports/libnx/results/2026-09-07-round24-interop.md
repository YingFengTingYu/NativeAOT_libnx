# 第二十四轮：部分原生库的直接调用边界

完整 LawnRuntime 调用图使用 stock Linux NativeAOT SDK 时，原生链接出现 350 个未解析入口：261 个 CryptoNative、15 个 NetSecurityNative 和 74 个 SystemNative。该数量是静态调用图的依赖范围，不等于启动过程会实际调用全部接口。

generate-interop-list.sh 从当前 libnx 原生库的真实导出生成清单。Libnx.PartialNativeLibraries.targets 在 SDK 设置之后替换三个库的整体 DirectPInvoke 配置，只直接绑定已有符号，其余导入保留 NativeAOT 标准延迟解析。此运行时不支持动态加载，因此未实现接口被调用时会明确失败；没有添加空的成功实现，也没有复用 Linux 原生库。

清单内容逐项写入 ILC 响应文件，导出变化会使增量编译失效。配置只由启动探针显式启用；原有基础探针仍使用严格静态链接。新增 ChDir 连接已有 SD 根路径转换，Realloc 连接 newlib realloc。链接脚本开始时清除旧 NRO，避免链接失败后误运行旧产物。

验证：完整启动目标成功链接。模拟器中显式调用未实现的 SystemNative_Socket，确认抛出带 libnx 不支持说明的 DllNotFoundException（阶段 19）；随后切换目录并执行真实 LawnRuntime.Initialize（阶段 20、21）。实际游戏已呈现标题界面，但退出保存问题尚未解决，不宣称所有 BCL 原生接口或网络能力已经可用。
