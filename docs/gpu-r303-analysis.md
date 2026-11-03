# r303 图形异常分析（2026-11-03）

r303 已启用完整替换后的 GPU 用户态栈，但画面验收失败。用户报告滑动时整个桌面
在不同颜色之间严重闪烁、紫色区域重现，动画主观上略好。不能把该轮记为渲染修复或
性能通过。依照用户要求，本轮结束后只回收、分析日志；设备保持 Recovery，没有启动
下一轮验证。

## 证据范围

当前轮原始 XNU 日志已通过 run ID 和 CRC 校验。Recovery 中只读回收根卷，全部
256 个分块与设备内容一致，并再次确认分块索引稳定；未请求写入 userdata。
文件系统日志重放只发生在本机分析副本，随后以只读方式挂载分析，分析挂载均已卸载。

初始化日志和管线日志以 r302 结束快照作内容哈希比较。旧目录中的 `pipelines.log`、
`stages.bin`、CPU profile 和 shader prime 日志全部未变，不能用作 r303 的新证据。
新日志来自共享缓存 `org.unixnu.metal`；私人镜像、统一日志、shader 缓存和设备标识
留在本机，不进入公共源码。

统一日志包含 87 次启动，不能按相同 PID 或重复的墙钟时间混用。对 r302/r303 快照的
timesync 内容比较确认恰好新增一次启动，再按解码后的 boot UUID 精确筛选，得到
本轮 1,142 条所选进程的图形关键词记录。查询范围和完整回收的日志均保留在本机。

## 已确认的运行情况

| 观察 | 本轮记录 | 能说明什么 |
| --- | --- | --- |
| backboardd 设备创建 | 成功，运行时 ID `3486f0413be9ea3e231869495c8f5be9` | 确实进入新运行时 |
| backboardd 内核连接 | 打开 2 次、关闭 1 次，保留 slot 0 | 与 r302 全部关闭的发布失败不同 |
| 原生提交 | 3,933 批、69,065 个 IB，单批最多 61 个 IB | GPU 有实际工作，扩展批量接口被使用 |
| 超过 16 IB 的批次 | 1,302 | 并非仅执行发现阶段的少量自检 |
| 显示复制 | 提交、完成均为 3,733 次；队列等待超时 0 | 原显示路径持续复制，但不能证明像素正确 |
| 渲染管线创建 | 207 条，全部 `ok=1` | 这些创建请求成功，不代表所有 draw、绑定或成帧都正确 |
| 对象协议注册 | 19/19 | 本轮对象协议注册完整 |

PosterBoard、AegirPoster 和 PridePosterExtension 也记录了同一新运行时的设备创建成功。
所比较的崩溃报告集合没有新增或改动的报告；这不等于所有运行时错误均为零。
本轮未开启完整 command-buffer / Turnip pass 诊断，不能据管线创建日志排除提交后的错误。

本轮筛选到的 Error / Fault 包括：QuartzCore 的系统预编译管线库不可用 1 条；
SpringBoard 的 Siri 组件取不到 Metal device 1 条；CoreGraphics 字体渲染样式的空
context 提示 81 条；PridePoster 的 renderer 尚不可更新 3 条；PointerUI scene 提示
1 条。它们在此前多次启动中也出现，尚未与本轮闪烁帧建立对应关系。SpringBoard
不在当前 GPU 客户端名单中，Siri 的局部失败不能推导出 backboardd GPU 未启用；
系统预编译库的提示也不能等同于随后全部 207 条动态管线创建失败。

面板只读计数在 4.002234 秒内增加 482，约 120.43 Hz；软件通知也配置为 120 Hz。
两者都不能作为桌面 FPS。3,933 次 GPU 提交、3,733 次 CPU 复制也不是独立完整画面计数，
不能用总数除以运行时间推断动画帧率。

## 最具体的移植遗漏：BGRA 下的 GMEM 限制

固定参考 AdrenoMetal `171d30b0` 的 `NativeAdrenoMetalPlugin.mm` 在设置 Turnip 策略时，
对 8 位 BGRA framebuffer 条件明确选择 `sysmem,nocb`。相邻注释记录了参考实现即便
应用 GMEM swap 补丁，仍有一帧桌面红蓝交换，因此保留该条件。该描述属于参考项目的
测量记录，不是本机 A750 的复现结果。

r303 的 houji 适配把该处替换为 `houji_turnip::configure()`，构建结果为
`autotune` / `nocb`，漏掉了 BGRA 条件下保留 sysmem 的限制。只读快照中没有
`/private/var/ngpu/tu_debug` 覆盖文件；检查的 496 个 launch plist 没有 GPU 环境覆盖项，
实际启动参数也没有 `sheng-tu-debug=`。这些支持默认策略会生效，但没有进程环境转储或
逐 pass 记录，不能据此确定发生闪烁的那一帧究竟采用哪种模式。

新 Mesa 中的 `turnip-render-mode-for-attachment-memory` 不会把所有导入纹理都强制为
sysmem。它允许边缘不满足 GMEM 对齐的线性导入图像通过 draw / 2D blit 加载和存回，
并继续在 GMEM 渲染；尚未分配 backing 的 memoryless attachment 也倾向保留在 GMEM。
`turnip-gmem-mutable-swap` 又改变了线性图像在 GMEM load/store 两侧的通道交换处理。
因此允许 GMEM 确实打开了此前强制 sysmem 时不走的路径。

这是新增严重闪屏的首要排查方向，但尚未证明是根因。r300 构建强制
`sysmem,nocb`，用户仍报告原有紫色区域，所以 GMEM 不能独自解释历次全部问题。

## 第二组疑点：原地写入、扩展色彩格式与资源同步

新 bridge 显式选择 `HostPageTexturePolicy::InPlaceWrites`，允许 GPU 直接写入导入的
IOSurface 页面；管线等待采用 `Exact`。这改变了旧栈依靠 shadow image 拷入/拷出的
资源路径。GMEM load/store、同一 surface 的别名、缓存可见性及复用时机需要一起核对。

本轮管线第一颜色槽的格式如下。它们是**管线创建条数**，不是执行次数或所占帧数。

| Metal 格式 | 条数 |
| --- | ---: |
| 80 / BGRA8Unorm | 153 |
| 550 / RGB10A8_2P_XR10 | 35 |
| 554 / BGR10_XR | 17 |
| 551 / RGB10A8_2P_XR10_sRGB | 1 |
| 115 / RGBA16Float | 1 |

即使最终 framebuffer 为 BGRA8，CoreAnimation 仍创建两平面 XR、半浮点、MRT 和
framebuffer-fetch 管线。关闭 deep framebuffer 并未消除全部扩展色彩中间结果。
旧紫色区域还需要检查这些结果的平面布局、load/store、混合、输入附件及跨 pass 复用，
不能仅根据函数成功编译就排除 metal2vulkan / Turnip 之间的语义问题。

代码审查没有发现当前显示复制故意绕过 GPU fence：它在 issuer 锁内检查 producer fence，
并重新检查当时的 ring tail，完成后保持锁进行缓存维护及 CPU 复制。新前端的普通完成
回调也仍在 GPU 完成之后；提前完成回调开关返回 false。这不能证明整条 IOSurface
生命周期无竞态，尤其不覆盖尚未提交的生产者、CPU 侧并发写入或跨进程所有权。

显示源观察器在 fence 和缓存维护之后、复制之前采样：3,731 个源 surface 的 64 点采样
累计得到 3,000 个偏紫候选点，峰值为一张源 surface 的 24/64 点。这只是宽松色相计数，
正常壁纸颜色也会进入统计，没有期望图像，不能把这些点全部判定为损坏，更不能将
3,000 个候选点误写成 3,000 张坏帧。

## 性能与诊断边界

207 条渲染管线创建的 CPU 墙钟时间合计 3.733488 秒，最长单条 336.841 ms；这只是
创建开销，不是 GPU 渲染时间，不能把全部动画掉帧归因于首次编译。BO 池峰值达到
512 MiB，并发生 233 次池外分配；最慢记录的独立分配为 64 MiB / 9.741 ms。内核方法
也有超过 20 ms 的 CPU 调用区间。它们说明仍存在延迟来源，但缺少逐帧关联数据。

主机的 Metal / MoltenVK 定义像素对照及 12 组 iOS16 QuartzCore shader 配方检查只覆盖
主机后端和所选配方，未执行 A750 的 shader、GMEM 或原生 IOSurface 同步，不能抵消
用户本轮观察到的严重异常。

另有一个诊断入口问题：插件中的 `else unsetenv("SHENG_DIAG")` 目前关联到
`sheng-gpu-pixels` 条件，单独的 `sheng-diag` / `sheng-gpu-trace` 可能被它清除。
编译期开启的 stage trace 会重新设值。本轮构建关闭 stage trace，也没有上述启动参数，
因此该问题不能解释本轮闪屏，但会妨碍后续仅打开 trace 的诊断。

后续应先在源码层恢复参考 BGRA 策略并核对诊断开关，再分别核对原地写入和 XR / fetch
路径，避免一次同时改变多个变量。若再次进行实机对照，需要把同一画面的 pass 模式、
纹理格式/别名、提交与完成和最终颜色关联起来；仅有管线创建成功或 fence 前进不够。
本文只记录分析及后续验证条件，没有安装修正、启动下一轮或宣称问题已解决。
