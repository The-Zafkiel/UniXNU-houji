# iOS16 / A750 图形栈替换

用户否定了 r300 的局部修补：动画卡顿和紫色区域完全没有变化。当前候选重新组装完整
用户态图形栈，保留 houji 的板级内核与恢复能力。r302 暴露发布入口适配遗漏；修正后的
r303 已确认实际 GPU 提交，但出现严重颜色闪烁和紫色区域，画面验收失败。详细证据与
疑点见 [r303 分析](gpu-r303-analysis.md)。本轮结束后按用户要求只分析，不启动下一轮。

| 组件 | 当前来源 |
| --- | --- |
| 完整 Metal 前端 / Rust bridge | UniXNU/AdrenoMetal `171d30b0400cbcdb26d304a908b552781ef840da` |
| 组件组合与 Mesa 补丁 | UniXNU/UniXNU `bd3e9472422e01c68d17237b5b8b869baefbd0f2` |
| metal2vulkan | `624bab3918de5b978e1101517abca2b4fa426565`，development 分支，配套 28 个补丁 |
| Mesa | `85fda3b36405bcaea8bffc64aa4f2651dc90770d`，配套 Darwin / GMEM / IOSurface 补丁 |
| 小米15对照 | superboyyy/hakios-arm64e-dada `f1ae906fcc43ba8f763663257724ba938fb820ab` |

`sources.lock.json` 保存补丁清单及哈希。运行时覆盖层来自干净固定提交应用完整补丁后
的变更文件，不复用旧 `219f0136` 的运行时覆盖层。Mesa 基础树已与上游提交的 Git tree
`b72ff1eb2138026ed6aba524770899faabf7d7c8` 比对一致。诊断补丁只补齐缺失的
`new file mode 100644` 元数据，源码内容不变。

小米15采用 A830 / SM8750、iOS26 / arm64e，不能直接提供小米14 A750 / SM8650 的
内存地址、寄存器或内核协议。当前保留 iOS16 的 arm64、528 字节 Metal 基类检查和
通过 Objective-C 元数据初始化的纹理尺寸限制。保留编译期 Metal 协议引用、命令缓冲
独立字典、稳定 trace identity、异步编译回调以及有界 VM 对象身份检查。

新的 Core Image、stitching、原地纹理写入、资源生命周期和运行时路径来自完整配套版本。
额外修正函数常量的 `required` getter；上游只有 `isRequired`，公开 API 调用会抛异常。
保留 BO 稀疏槽在退休锁内清零的并发修复。

内核仍是 ABI v1：method 5 接收 416 字节 / 16 IB；Query flag 2 才允许 method 17 的
3,104 字节 / 128 IB。ABI v2 的 method 17 与 flag 2 含义不同，禁止按其调用或选择
cache mode。未知优先级/日志方法返回不支持；自有 BO 不宣称 IO coherence。失败提交
只传播错误；只有明确未接收的 busy 可按上游路径等待。

houji 没有新版 GPU scanout handoff；它保持关闭，显示仍走原板级复制路径。移除了
针对其他 iOS 缓存地址的读写代码。默认禁用后台全库预热和 CPU sampler，避免在动画
期间与新缓存首次编译争用 CPU。默认单采样，保留环境变量对照；Turnip 默认
`sysmem,nocb`，恢复参考实现对 BGRA framebuffer 的限制，自动 GMEM 仅作显式对照。
r303 的 `autotune` / `nocb` 产生严重闪烁；恢复限制的 r304 不再整屏闪烁，但紫色和
动画卡顿仍在。r304 记录到 25,586 个 SYSMEM pass，未记录 GMEM pass。

r304 同时修正诊断开关的条件关联，并在自有缓存目录保留实际 `TU_DEBUG` 和低频
telemetry 文本。每进程最多 32 个快照、256 KiB，不在渲染线程写盘。完整 pass 诊断
仅在本次有时限的实机对照中开启，CPU sampler 和全库预热继续关闭，不能用于直接
比较正常配置的 FPS。运行时、Turnip 二进制、原地写入策略和内核显示同步均沿用 r303。

用户要求 r305 不采集运行日志，进行主观流畅度对照。该候选采用 `--no-gpu-logging`：
关闭 Metal 初始化/管线/telemetry 文本、runtime 消息及 shadow 日志、pass/帧追踪和
采样线程。r305 错将 `houji_ufs_log` 设为 0；该位也是 canary/固件启动资格检查的前置
条件，导致用户无法进入系统，不能作为流畅度对照。修正配置恢复该启动前置条件，
通过 `gtrace=0` 独立关闭 live handoff 之后的桌面周期报告、快照与终端诊断转储；
不启动 Recovery 日志回收器，也不在该轮之后自动提取用户态日志。正常资源生命周期、
故障处理、shader 缓存及有时限的 Recovery 保护保留。此轮依赖用户观察，不会产生
可用于声称实际 FPS 的日志数据。

启动参数检查现在拒绝在启用 GPU bring-up 时关闭 root/log/recovery 任一必需位；
`gtrace=0` 仍合法。该检查运行于实际编译 UEFI 参数审核，不依赖设备失败后采集日志。

主机真实 Metal / MoltenVK 对照的定义像素测试，已分别覆盖 requested / single 采样，
包括清屏、加载、resolve 保留、半浮点、混合、memoryless、分层多 mip 拷贝和函数常量。
这些检查不执行 Adreno shader，不证明桌面画面或帧率。只有本候选的实机画面与运行日志
可以确认紫色区域、应用动画和渲染稳定性是否改善。

发布入口使用 houji 现有 `.clients` 名单，明确选择 backboardd 与各客户端；不依赖
镜像中不存在的 `.publish`，也不会在 A750 的发现阶段执行上游默认 A740 原始自检。
每个进程在已验证的自有缓存目录保存最多 32 行 / 24 KiB 初始化记录，记录完整设备
创建错误和成功时的运行时 ID。缓存日志不改变 GPU 提交或错误传播。
