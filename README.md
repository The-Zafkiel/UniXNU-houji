# UniXNU-houji

> 开发源码快照，GPU 未完成；构建依赖、设备绑定与发布范围见 [源码快照说明](SOURCE_SNAPSHOT.md)。

小米 14（23127PN0CC / houji / SM8650）的 UniXNU 移植工作树。
目标是运行原生 iOS 16.7.16 用户态，逐步完成硬件适配。

当前阶段（2026-10-25）：UEFI → DarwinBoot → 八核 XNU → 原生 UFS/HFSX 根卷
→ iOS 桌面已在小米 14 显示；r56 由用户确认 1200×2670 全屏、触控准确，
设置列表滚动相较 r54 明显改善。但进入“关于本机”会导致可见界面无响应，正在定位；
内核仍可保存日志并自动返回 OrangeFox。稳定回退保留 r54。
r58 加入边缘触点标记与原生 `pid_hibernate` 系统调用后，用户确认最近任务卡片可用。
“关于本机”已定位到网络接口枚举等待。按用户最新优先级，当前首先推进 GPU。
本轮使用 8 GiB 内存、两颗 A520、五颗 A720、一颗 X4 和参考项目配置的系统卷启动基线，
保留 Setup 与激活服务；r53 合并首次设置完成标志，r54 加入官方开发板 ADT 属性，
在没有 Wi-Fi 的情况下越过首次设置。r47 的 120 次用户态采样及返回 Recovery 后的日志校验通过。
r52 已由用户确认 Hello／设置界面可实际触摸操作；日志记录 78 个物理触点报告及
78 次 SweetHID 调用。Wi-Fi 等其他硬件和完整 iOS 功能仍未验证。
定位服务的消息等待仍存在，首次启动采用版本迁移基线越过等待，未修复定位。
参见 [启动基线与参考记录](docs/migration-upstream-research.md)。
无 Wi-Fi 时的可恢复首次设置配置见 [离线跳过设置](docs/skip-setup.md)。

用户已明确允许清空系统和个人数据，12 GiB iOS 系统卷已写入现有 userdata。
安装后完整 O_DIRECT 读取的 SHA256 与源镜像一致；根卷保存在 UFS，未上传 RAM。
37 个关键分区、六个 LUN 的双 GPT 及原 recovery 均已备份。当前读取环境是
OrangeFox root ADB；请保持恢复环境，不要让 Android 初始化 userdata 或在 recovery 格式化 data。
详见 [实机记录](docs/bringup.md)。

[CPU 接入与调度参考](docs/cpu-scheduling.md)记录了 ArmCpu、Ace 5 Pro、Linux 和
XNU 的实现比较。r55 实机通过八核 MIDR、绑定任务、定时中断和核间中断预检，
r56 八核已运行 iOS 用户态。当前采用 Clutch SMP，尚未完成大小核能力感知调度或动态调频。
CPU0 旧版短时只读采样约 1.69 GHz，不能代表八颗核心当前频率。
显示仍由 CPU 渲染；[显示与性能记录](docs/display-performance.md)区分已验证结果与待修问题，
[GPU 调研](docs/gpu-research.md)记录 A750 和其他平台最新代码的可复用边界。
GPU 专项已实机通过 CX/时钟、CM3 与 DMA 正反例、原厂 GMU/HFI 启动，
以及八条调度初始化消息。独立 GPU/GMU 页表的实际遍历、上下文配置回读、
多类权限和七块普通缓冲区分配应答已验证；4 MiB 安全数据的 CPU 访问撤销和
Qualcomm HLOS→CP_PIXEL 真实所有权转换也已实测通过。r92 已通过独立安全页表
共享、bank3 配置回读和安全内存应答。r93 已完成全部 10 条分配与 HWSCHED START；
r94 已验证 GPU OOB 唤醒、电源/时钟就绪及受保护的状态读取，
r95/r96 已验证 SQE/AQE 微码内存及地址绑定、101 步 Gen7 配置和 75 对恢复记录。
正在验证首次 CP 初始化；尚未完成可用的 GPU 命令提交、
fence 或 Metal 桌面加速。本机 ZAP 固件已从 odm_a 只读取得，尚未执行。
专项测试合并采样并直接返回 OrangeFox；启动命令自动后台轮询，返回后抓取
运行编号与 CRC 校验的日志。这些 UEFI 专项不启动 XNU，不改 iOS 系统卷。

Recovery 已确认 Synaptics S3910／TouchCom v1、10 点触摸和 1199 × 2669 原生坐标。
触控已通过原厂电源／时钟交接、GPIO SPI、芯片默认 TouchCom 描述符接入 XNU HID，详见
[触控适配记录](docs/touch-bringup.md)。

- 固件源码在相邻 `uefi-houji`，以 Mu-Silicium `42fe5637` 为基础，新增 `houjiPkg`。
- 内存来自当前设备的 xbl_config 和 live FDT；41 段内存由 libfdt 独立核对。
- OEM EnvDxe 来自本机原厂 uefi 分区的只读备份；二进制在仓库外。
- 第一阶段固件不装载存储驱动。变量保存在 RAM；Capsule 协议始终拒绝更新。
- 使用 `fastboot boot` 临时启动。检查点通过 BCB 返回 OrangeFox，保留 misc 的
  reserved/A/B 数据。recovery_a 已安装经过验证的 houji 恢复镜像；GPT 分区边界不变，
  sde 两项启动槽位属性的修复变化已单独审计并固定。
- [iOS 测试输入](docs/ios-input.md)固定为用户提供的 iPhone X 16.7.16 / 20H392。
- Apple XNU 8796.141.3 的早期内核和后续 GUI/HFS 兼容内核分别保留。
  当前 GUI 内核已实机挂载根卷并运行用户态；构建仍属于实验性开发配置。
- 单核内核、Apple ADT 和 DarwinBoot 装入 64 MiB 只读 FAT RAM 盘。
  第二版把整个 RAM 盘嵌入压缩 Image，避免依赖 Android 引导器对 boot ramdisk 的选择。
  驱动仅在已保留的 Kernel 内存区寻找标签、大小和 CRC 均匹配的载荷，不扫描任意物理内存。
- 第三版 ADT 补齐 NVRAM 属性和一个空的 8 KiB CHRP proxy bank，借助实际 XNU
  源码中的检查函数验证头部 checksum、Adler32 和分区边界；没有物理 NVRAM 控制器。
  实机已越过第二版的 NVRAM panic。
- UEFI 诊断文字及 XNU 正常检查点的消息尾部已能保存到 logfs，返回 OrangeFox
  后核验运行编号和 CRC 并取回。实时 USB CDC ACM 与任意 panic 自动保存仍未实现。

## 已解决的启动问题

1. `EnvDxeEnhanced / AutoGen.c:650`：保留区必须命名为 `FD_Reserved`，供
   PlatformPeiLib 从原厂固件保留区发现 Scheduler Interface HOB。第二次启动确认通过。
2. `DxeMain.c:561`：补齐 Capsule 架构协议。设备专用 NoCapsuleRuntimeDxe
   对更新和查询能力均返回 EFI_UNSUPPORTED，不包含固件更新实现。
3. `BmDriverHealth.c:553`：精简固件没有 FormBrowser2；按上游说明把
   PcdDriverHealthConfigureForm 设为 ZeroGuid，关闭对应修复页面。第四次启动确认进入菜单。

## 本地构建

`scripts/build-uefi.sh UEFI_ROOT UNIXNU_ROOT VENDOR_ROOT` 在已准备好的 Linux ARM64
构建环境中调用固定 Mu BaseTools 和 CLANGPDB。`VENDOR_ROOT` 必须含本机提取的
HoujiVendorPkg 和 provenance manifest。构建脚本本身不操作设备。

构建 BootShim 后，`scripts/pack-uefi.py` 生成 Android header v4 的 RAM 启动镜像，
并解包逐字节比对 BootShim、FD、DTB，验证 ARM64 Image 大小和错误输入拒绝行为。
使用当前设备采集的 DTB，不使用参考机的设备树。

带 XNU 的构建需先以 `make-ram-payload.py --inputs PAYLOAD_DIR --output PAYLOAD.img`
生成 RAM 盘，再设置 `HOUJI_RAM_PAYLOAD=PAYLOAD.img` 运行固件构建。
`pack-uefi.py --ramdisk PAYLOAD.img --payload-in-image` 把载荷放在 FD 后的 4 KiB
边界，更新 ARM64 image_size；BootShim 仍只把 FD_SIZE 字节重定位到固件地址。
解压后的完整 Image 小于 256 MiB Kernel 保留区。没有给 Android boot header 添加 ramdisk，
也不依赖参考机的高位 DDR 地址。

`scripts/prepare-ios16-early.py` 仅选取固定 Vermeer 参考版本的早期 20 个补丁，
调整屏幕与 CPU 匹配；不引入参考机的 UFS、USB、触摸或电源驱动。
该早期内核只用于内存中验证内核入口和单核调度，不含系统卷。

当前 XNU 主机编译适配包含 Xcode 27 IIG 输出转换：保持 Darwin 22 原有的
四字段 IORPC ABI，通过既有 Mach 消息解码函数访问内容；没有修改运行时 IORPC 布局。
内核版本属于 Darwin 22 的早期移植基线，并非 IPSW 中未经修改的 T8015 内核。

## Mac 日志

`scripts/capture-usb-log.py` 可列出 Mac 当前的 CDC ACM 串口，并在明确选择端口后采集原始日志。
在手机固件的 USB 串口驱动完成前，它不会使手机自动获得 USB 功能。

```sh
python3 scripts/capture-usb-log.py
python3 scripts/capture-usb-log.py --port /dev/cu.usbmodemDEVICE --output /absolute/path/uefi.log
```

存储诊断快照已在实机验证，可用下列命令只读取回：

```sh
python3 scripts/capture-logfs.py --adb-port 5038 --recovery --output /absolute/path/new-capture
```

实际端口以 ADB 连接为准，支持 5037/5038/5047。`UniXNU.log` 只含 UEFI 应用
文字；`XnuLog.bin` 为已提交的原生快照，校验通过后导出 `XnuLog.txt`。
后续运行先归档上一轮文件，再重新绑定运行编号、预分配数据扇区和启动镜像；
旧镜像不会静默覆盖另一轮日志。
