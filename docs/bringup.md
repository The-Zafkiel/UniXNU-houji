# 小米 14 单核 XNU 实机记录

2026-10-23，在已解锁 Bootloader 的 23127PN0CC / houji / SM8650 上使用
`fastboot boot` 临时启动，全程没有 flash、erase、格式化或分区表操作。

## 已确认结果

第三版 XNU 临时镜像在原生硬件上依次完成 UEFI、只读 RAM FAT 文件系统、
DarwinBoot、Darwin 22.6.0 内存初始化、NVRAM proxy 解析、平台专家及单核 CPU 初始化。
用户提供的 IMG_0655.MOV 最后清楚显示：

```text
HOUJI CPU: matched SM8650 boot CPU provider
HOUJI CPU: starting registered boot processor
MAC Framework successfully initialized
SWEET CPU: boot CPU 0 registered, SGI1 verified, initialization complete
```

`SWEET` 是早期移植内核沿用的编译配置名称；实际 ADT、匹配项、CPU 和 framebuffer
已改为 houji。该完成日志位于 `ml_processor_register`、`processor_start` 和 SGI1
真实中断自测之后。ADT 只有 CPU0，CPU 驱动会拒绝多 CPU 拓扑；没有启动其他核。

## 各次结果

| 镜像 | 实机结果 | 处理 |
| --- | --- | --- |
| UEFI r1 | EnvDxeEnhanced AutoGen.c:650 | 恢复 FD_Reserved 内存区名称以发现 scheduler HOB |
| UEFI r2 | DxeMain.c:561，缺少 Capsule 协议 | 添加始终拒绝更新的协议实现 |
| UEFI r3 | BmDriverHealth.c:553，缺少 FormBrowser2 | 按上游配置设 ZeroGuid，跳过健康修复页面 |
| UEFI r4 | 进入 UniXNU 默认三选项菜单 | 完成基本 UEFI 验证 |
| XNU r1 | 默认菜单倒计时 Return to firmware，未观察到载荷 | Android ramdisk 的传递方式尚不能依赖 |
| XNU r2 | Darwin 22.6.0 进入 VM 初始化，随后 NVRAM CHRP panic | 将 FAT 载荷嵌入 Image，并修正绝对路径；下一版补齐 NVRAM proxy |
| XNU r3 | CPU0 注册、启动和 SGI1 自测完成 | 达到本阶段目标 |

XNU r2 同时改变了载荷传递和配置路径；仅凭结果不能把 r1 的失败唯一归因于 ABL
忽略 ramdisk。嵌入 Image 的方案已经实机验证，仍无需添加物理存储驱动。

## 日志中的提示

- `"name" not a kext` / `"AAPL,phandle" not a kext`：XNU 的 OSKext 扫描器遍历
  memory-map 属性时遇到非 kext 名称，记录日志后继续；不是本轮停止原因。
- `gPanicBase is still not initialized`：没有配置 Apple panic 日志区，函数打印后返回。
  这影响 panic 日志保留能力；视频证明它没有阻止单核初始化。
- 目前停留在日志界面不代表 iOS 已安装。此阶段不包含可挂载的系统卷或用户态。

## 构建与证据

成功镜像：`houji-xnu-r3.img`，6,950,912 字节，SHA256：

`fe7398049ad4de05390c656f8c9681b3bb3c28a13d28107434ac4866c83a6833`

内核：Apple XNU 8796.141.3 / DEVELOPMENT_ARM64_SWEET，15,899,672 字节，SHA256：

`206c962e4c800b1781fb9af56d340e61f7c2681cabe75b2b4f9d7305feea4196`

DarwinBoot 固定于 `85332b317ee80898fc78939ea64e6ca52d3837a3`，修改 Darwin 22
boot_args 布局；Mu-Silicium 固定于 `42fe563791e51bf5500c2e470ed294ca0c94b97c`。
参考 Ace 5 仓库固定于 `18f51ec652501c62e19dd4f1035dc51daa2a3361`，Vermeer
早期补丁参考固定于 `d68e05df683f863f054b0f9c063c54d2b1342b2a`。

本地证据根目录：
`/Users/lonzzi/Documents/Codex/UniXNU-Xiaomi14-2026-10-23/native-port/`

- `evidence/uefi-xnu-r3-result.json`：实机结论、视频摘要、SHA256 和测试边界。
- `evidence/uefi-xnu-r3-single-cpu.png`：从用户视频提取的成功帧。
- `xnu-r3-trial.jsonl`：ADB / fastboot 命令及原始结果。
- `device-baseline/bootstate-before-xnu-r3.txt`、`bootstate-after-xnu-r3.txt`：
  返回 Android 后系统版本、槽位、vbmeta digest 和五个启动分区哈希完全一致。
- `firmware-xnu-r3.log`、`xnu-early-build-r3.log`：构建日志。

## 尚未覆盖

只完成单核早期启动，没有验证持续运行、其他 CPU、挂载系统卷、iOS 16.7.16 用户态、
GPU 加速、触摸、音频、网络、休眠或 USB 日志。用户提供的 iOS 16.7.16 / 20H392
仍是后续用户态测试输入；本次内核不是 IPSW 内未经修改的 T8015 内核。

Mac 端日志接收脚本已准备，但手机当前固件没有 USB CDC ACM 后端。
需要继续适配 SM8650 的 USB device 控制器、DMA 和日志输出链路后才能直接采集。


## 2026-10-24 后续实机进展

当前目标已继续到 iOS 用户态，尚未进入 iOS。以下结论有实机画面或返回 Android
后的采集支持，不把离线编译当作运行成功。

- `pstore-r1`（IMG_0658.MOV）报告在 A7200000 写入 130 字节标记；返回 Android
  后 `/sys/fs/pstore` 为空，没有验证跨重启取回。不能作为可用的自动日志通道。
- 扩展 SMMU 诊断在首个 S2CR 读取处出现外部访问异常；改为 SMR-only 后，首遍
  107 项扫描成功，但第二遍 SMR[0] 重读仍出现同类异常。因此不能断定是 S2CR
  特有权限限制，供电、访问时序和平台初始化仍待核实。
- `smr-single-r2`（IMG_0660.HEIC）单次扫描通过：一条有效显示映射，USB SID40
  没有有效映射。预检未写 SMMU、未启用 DMA。随后进入日历初始化，等待超过
  一分钟仍无完成标记；单核注册和 SGI1 自检再次通过。定时器诊断正在接续。
- `highram-probe-r1`（IMG_0661.HEIC）中，整个候选高区保持 EFI Reserved，
  只有六个指定 16 KiB 页面被逐页保存、两种模式写读并恢复原内容。6/6 页面
  全部通过，AT S1E1W 的 PAR 属性为 WB，地址转换符合目标。没有测试整片高区、
  6 GB 分配、XNU 高地址 RAM 或 USB 上传。
- 高内存测试返回 Android 后，五个启动分区哈希仍与原始基线一致。六个 UFS LUN
  的双 GPT 头、分区项数组已只读备份并核验 CRC；没有重分区。

完整 iOS 16.7.16 HFSX 系统卷和逐块验证的 5.63 GiB 压缩 backing 已在 Mac
生成。RAM COW 后端和 GUI 内核已离线链接；尚未在实机绑定、上传或挂载该根卷，
不能宣称 launchd、Setup 或 SpringBoard 已运行。

新增证据位于 `native-port/evidence/`，保留原图、镜像 SHA 和各次启动命令日志。
高内存候选默认关闭；`HOUJI_RAM_PROBE=1` 只能与 Conventional/RootRAM 功能
关闭时一起构建。实验镜像仍使用 `fastboot boot`，不刷写启动分区。


## 2026-10-24 持久化方案范围调整

用户先说明“安卓不用保留”，随后明确选择“系统和个人数据都可以清空”。这是最新范围，
替代早期保留 Android/userdata 的要求；整套压缩根卷上传 RAM 的路线已暂停。
后续先验证 UFS 和引导，再根据具体镜像/布局执行必要的清空与刷写，不为准备实验
提前破坏仍可用于 ADB 的 Android 环境。

37 个校准、通信/设备状态及 A/B 引导相关分区已只读备份，912,203,776 字节，
每份本地副本与手机端 SHA256 一致。manifest 位于任务证据根的
`recovery-backup-20261024/manifest.json`。其中包含设备特有数据，应留在本机，
不提交源码仓库。该备份不是完整 ROM 或用户数据备份，也不包含 RPMB 导出。

候选布局优先复用现有 `super_ext`（2 GiB）放 FAT32 引导文件，`userdata`
（约 480 GiB）放 iOS HFSX 卷；先避免移动任何物理分区边界。GPT 类型、启动项、
日志专用区域和文件系统最终大小待 UFS 只读验证后固定。此记录不是已刷写结果。

### UFS 原厂驱动前置服务预检

`storage-probe-r1` 的 IMG_0664.HEIC 显示 Clock、SCM、PlatformInfo 和 USB Function
IO 均缺失；因此应用没有启动 UFSDxe。仅找到一个内存盘 BlockIO，没有物理 GPT
通过，不能解释成 UFS 已可用。返回 Android 后五个启动分区与基线完全一致。

接续的 `foundation-r2` 限于本机提取的 SmemDxe、DALSys、HWIODxeDriver、ChipInfo、
PlatformInfoDxeDriver、ULogDxe、ScmDxeCompat。原始 PE 逐字节复制，保持本机来源
manifest；生成代码保留各驱动真实 DEPEX 的 GUID 合取，启动前逐项验证，并额外要求
真实 OEM Kernel/Scheduler、ULog 的 DAL 和 SCM 的 ChipInfo。缺失依赖或驱动返回
错误后停止，不伪造协议。此版本不包含 UFSDxe，不装载时钟、SMMU 或存储驱动。
当前仅通过离线构建，实机结果待单独记录。

`foundation-r2` 的 IMG_0665.HEIC 显示 OEM Kernel/Scheduler 的 EFI 协议缺失，
因此被应用的全局前置门挡住，七个服务均未启动。这不能证明调度器 HOB 缺失。
对本机 SmemDxe 的反汇编已确认：0x1c34 的构造函数先 LocateProtocol，失败后
在 0x1cf8 通过 GUID 8ec2bd8d-56d7-49ef-8796-631778f8ebf8 查 HOB，从 +0x18
读取真实接口地址。PlatformPeiLib 也实际构造此 HOB；r2 的门过严。
返回 Android 后五个启动分区再次与基线一致。

`foundation-r3` 将检查改为匹配原厂的 EFI 协议/HOB 两条获取路径。它核验 HOB
长度、接口及锁表在已映射 RAM 中、接口版本和 InitLock 函数位置，不发布替代
Kernel 协议、不修改原厂函数。其余七个模块及 DEPEX 门保持不变。已通过离线
编译与镜像解包校验，实机结果待反馈。

用户反馈 `foundation-r3` 自动返回 Android，未拍到屏幕，具体停止点未知。返回
Android 后 bootreason 仅为 `bootloader`，pstore 为空，五个启动分区哈希不变。
不能据此认定 Smem、SCM 或任何特定服务已执行或发生故障。

接续 `hob-only-r4` 默认只检查 HOB，15 秒停留后输出检查步骤；除既有 EnvDxe 外，
七个待测服务 PE 已从载荷删除，只有显式 `--foundation` LoadOptions 才会进入
服务加载分支，而该镜像的配置不提供此参数。用于区分 HOB 检查与原厂服务
初始化阶段，尚不作为重启问题已修复的结论。

组织仓库与 Wi-Fi 调查见 [上游模块对照](upstream-modules.md)。

`hob-only-r4` 的 IMG_0667.HEIC 显示 `HOB-only checkpoint PASS`：接口地址
`A703FD38`，版本 `0x1000B`，锁表地址 `A703FD10`，接口/锁表/InitLock 位置均通过
已映射 RAM 检查。没有启动额外 OEM 服务，也没有在此测试中调用该接口。
返回 Android 后五个启动分区再次与基线一致。该结果验证这次 HOB 读取路径可用，
仍不把 r3 自动重启归因到某个未观察到的服务。

`smem-only-r5` 将显式 `--smem-only` 限为服务表首项，载荷只有额外的本机原始
SmemDxe，缺少其他六个服务和 UFSDxe。LoadImage 前停 15 秒，打印加载结果与
基址后，StartImage 前再停 20 秒；结束后核对真实 SMEM 协议是否出现。若 SMEM
已存在则拒绝重复初始化。此版本用于区分映像加载与入口执行，实机结果待记录。

`smem-only-r5` 的 IMG_0672.HEIC 显示 LoadImage 和 StartImage 均返回 Success，
SMEM 协议由测试前 Not Found 变为测试后 present/Success。返回 Android 后五个
启动分区哈希不变。

`foundation-r6` 的 IMG_0673.MOV 中，前六项 Smem、DAL、HWIO、ChipInfo、
PlatformInfo、ULog 返回成功；尾段约 94–96 秒停在 `starting ScmDxeCompat NOW`，
99 秒已黑屏，之后用户确认返回 Android，没有观察到 SCM 返回。启动分区哈希
仍与基线一致。失败范围已缩小到 SCM 启动路径，内部具体位置尚未证明。

静态审计发现遗漏的真实依赖：SCM RVA 0x1e0c 查找 GUID
`9c1eb71f-dd6c-4ed5-9f6a-5cc0ca789f16`，随后通过其接口分配通信缓存；同一个
GUID 存在于本机 ShmBridgeDxeLA，其模块在参考机 APRIORI 顺序中位于 SCM 前。
`foundation-r7` 加入原始 ShmBridgeDxeLA，并把该真实协议加入 SCM 的额外门，
先确认提供者存在，再运行 SCM。这个依赖修正尚待实机验证，不能先认定重启已修复。

`foundation-r7` 已核对 Android 原始启动基线，并通过 `fastboot boot` 收到
Sending/Booting OKAY。镜像 SHA256 为
`c4a380ce786fa421817f8a86cc47d4c0a8a0a61b6a03554271800e37ba246f63`。
此确认仅表示引导程序接受临时镜像；ShmBridge、SCM 及完整服务序列结果仍待屏幕反馈。

`foundation-r7` 的 IMG_0675.HEIC 确认 ShmBridge 与 SCM 的 LoadImage、StartImage
均返回 Success，`foundation result=Success`，真实 SMEM、SCM、PlatformInfo
协议存在。Clock 和 USB Function IO 未加载，因此 Not Found；仅有内存盘，0 个
物理 GPT 通过符合该版本范围。返回 Android 后五个启动分区哈希与原始基线一致。

时钟前置审计确认原厂 ClockDxe 的 DEPEX 还要求 CmdDb GUID
`1c34f691-d33d-4e14-b8f8-32c6c029e95b`，它不是 SCM GUID；其字符串也有
PwrUtils/NPA 的 LocateProtocol 失败路径。Ace 5 APRIORI 顺序为 CmdDb、PwrUtils、
NPA、Rpmh、Vcs、Clock。接续 `power-bootstrap-r8` 先测试前三项，共 11 个服务；
保持原始 PE/DEPEX，NPA 额外要求真实 DAL。只有显式 `--power-bootstrap` 才进入
新分支，原 `--foundation` 保持限于前八项。r8 不包含 Clock、SMMU、UFSDxe。

`power-bootstrap-r8` 已通过离线编译、FAT 文件读回和镜像结构检查，11 个原厂
PE 与本机提取源逐字节相同；先前八项不再停顿，后三项各在 LoadImage 前停
2 秒、StartImage 前停 10 秒。临时启动前基线不变，Fastboot Sending/Booting
均返回 OKAY。镜像 SHA256 为
`11a850aab7391359498ce5c97cc6fd01fa1f46b7b136f8e7ec9a644039e2b464`，
服务执行与协议发布结果仍待实机画面确认。

`power-bootstrap-r8` 的 IMG_0676.MOV（57 秒尾帧）确认 CmdDb、PwrUtils、NPA
均返回 Success，三个真实协议均存在，`foundation result=Success`。返回 Android
后五个启动分区与原始基线一致，ADB 当前由 5047 端口持有。

`clock-bootstrap-r9` 显式增加 Rpmh、Vcs、Clock 三项，保持前 11 个原厂 PE
与 r8 相同。根据真实 LocateProtocol 路径及 Ace 5 的 APRIORI 顺序，为 Vcs
额外核对 NPA、PwrUtils、Rpmh，为 Clock 额外核对 NPA、PwrUtils、Rpmh、Vcs。
六个电源/时钟提供者在返回成功后还必须存在非空的实际协议接口，否则立即停止。
旧 `--foundation` 和 `--power-bootstrap` 分别仍限于 8、11 项；只有显式
`--clock-bootstrap` 才运行到 14 项。新三项各暂停 2 秒后 LoadImage、10 秒后
StartImage；不包含 SMMU 或 UFSDxe，实机结果待记录。

`clock-bootstrap-r9` 的 IMG_0678.HEIC 确认 Rpmh 和 Vcs 返回 Success 且实际协议
存在；Clock LoadImage 成功，停在 `starting ClockDxe NOW`，用户确认持续超过
一分钟无返回。右下出现一条水平噪点条，尚不能确定是时钟/电源操作、断言输出
还是显示缓冲问题。返回 Android 后五个启动分区哈希不变；再次取回的原厂 logfs
文件仍没有本端口日志或可归属此次 Clock 调用的异常信息。

接续 `clock-mmio-r10` 先运行已通过的前 13 项服务，然后只对 live FDT 的六组
时钟寄存器首尾页执行 AT S1E1R/W，并输出 PAR 与 GCD 描述。ClockDxe 文件已从
FAT 删除，显式 `--clock-mmio-check` 也把执行上限限制为 13；不执行 ClockDxe，
不读取或写入外设寄存器，不改映射。页表查询不能证明硬件寄存器可访问，也不能
证明整个范围已映射；更不能在结果出来前把映射缺失当作本次卡住的原因。
六组区间已逐项与本机 FDT 比较一致，保留 PAR 原值并在检查后恢复。

`clock-mmio-r10` 已编译、打包并校验 FAT 内容，镜像 SHA256 为
`6bb8604fc15d2e950bac81d0ba5d501a0bab01c11012edec184da6dbf202fa0f`。
启动前 Android 基线不变，Fastboot Sending/Booting OKAY；地址转换和花屏对照
结果仍待实机反馈。

`clock-mmio-r10` 的 IMG_0680.HEIC 确认 `fault-pages=10`、`nonidentity-pages=0`。
TCSRCC 首尾页 R/W 转换成功，GCD 类型 3；其余五组受测页转换失败，照片可见
PAR `0x80D`、GCD 类型 0。图中未见之前的水平噪点条。返回 Android 后五个启动
分区仍与基线一致。这证明 Clock 启动前有映射缺项，但不能单独证明 r9 卡住原因。

接续 `clock-mapped-r11` 添加默认关闭的 `HOUJI_CLOCK_MMIO=1` / PCD 开关，仅
补入本机 live FDT 的 GCC、CAMCC、DISPCC、GPUCC、VIDEOCC 五组 NS_DEVICE
MMIO 映射，末端按 4 KiB 向上取整。保留原有 41 项与已有 TCSR 映射。编译实际
C 映射函数的六种开关组合测试确认：关闭开关时输出与原源码完全相同，开启时
只追加五项，所有区间对齐且不重叠，并与 live FDT 范围一致。该主机测试不证明
ARM 页表或硬件寄存器可访问。

r11 显式 `--clock-mapped` 先执行已通过的 13 项，再检查十二个首尾页的 R/W
转换、身份映射及 MMIO/Device 类型；任何一项不符就停在 Clock 之前。通过才
单独加载原始 ClockDxe，不重复初始化前 13 个服务。同步异常与 SError 的 PC、
ESR、FAR 报告只在 CPU 协议允许注册时启用，不替换已注册的回调；Clock 返回
后撤销本次注册。未加入 SMMU/UFS 驱动或磁盘写入，实机结果待记录。

`clock-mapped-r11`（镜像 SHA256
`4ac63915559ea644c157be23689d55e52e3f3a7011ea496878b380045190832a`）的
IMG_0683.HEIC 确认 fault-pages、nonidentity-pages、wrong-type 均为 0；异常报告
的同步/SError 两项均注册成功。Clock LoadImage 成功，基址 D2858000、大小
38000，照片最后一行是 StartImage in 8 seconds。随后用户报告自动返回 Android，
未录到后续步骤，不能声称 Clock 返回、异常回调触发或缺项修正已解决重启。
返回 Android 后五个启动分区哈希不变，原厂 logfs 文件仍无本端口输出。

用户准备录像并明确回复“启动吧”后，以 `clock-mapped-r11-repeat` 标签重新
临时启动完全相同的镜像；Fastboot Sending/Booting OKAY，视频结果待反馈。

`clock-mapped-r11-repeat` 的 IMG_0684.MOV 约 56–59 秒保留了完整错误：
`Failed to map PhysicalAddr:0x3D00000, size:0x100000, SetMemorySpaceAttributes`，
随后为 `Clock_InitBases failed`、`ASSERT ClockDriver.c +6700: 0`；60 秒帧已黑屏，
用户确认自动回到 Android。无 Clock 返回状态，也未观察到同步异常报告。返回
Android 后五个启动分区哈希不变。视频中数字曾因模糊难辨，最终以清晰帧 OCR
及原厂表交叉核对，不使用先前猜测的其他地址。

原厂未修改的 DALSys SHA256 `949984d39680c9a4034eeabb66aa743d500f837dbd1c0133721ae19495be6635`
在文件/RVA `0x40A68` 的记录包含名称 `GFX`，`0x40A70` 为物理基址
`0x03D00000`，后续大小 `0x100000`。这是包含 Linux GPUCC 子银行的父窗口；
r11 仅映射 `03D90000+A000`，不能覆盖这个实际 HWIO 请求。r12 将同一映射
扩展为父窗口，不增加重叠项，不改 Clock PE、不绕过断言；映射开关仍默认关闭。

r12 的主机测试以实际 DALSys SHA、GFX 表字段与 live FDT 的包含关系为依据，
六种编译开关组合均通过默认布局不变、范围对齐及无重叠检查。实机前置检查增加
GFX 父窗口首尾页；通过后才执行原始 ClockDxe。此修正尚未实机通过，不能宣称
Clock/UFS 可用或重启问题已解决。

日志研究、只读取回脚本和测试边界见 [启动日志采集](log-capture.md)。

`clock-mapped-r12` 的 IMG_0685.MOV（48.255 秒）确认前置抽样页检查三项为 0，
Clock 仍未返回；新的失败是 `PhysicalAddr:0xAA00000, size:0x200000` 的
SetMemorySpaceAttributes，随后为 `Clock_InitBases failed`、
`ASSERT ClockDriver.c +6700: 0`。用户确认自动回到 Android。返回后五个启动
分区、slot/build/vbmeta digest 与原始基线一致。原厂 logfs 再次取回的六份文件仍无
本端口日志；PMIC PON 环记录含 PS_HOLD 的 warm/hard reset，但没有可将其中某条
唯一归属于本次故障的时间信息，因此不据此认定看门狗或其他具体复位原因。

继续审计原始 ClockDxe（SHA256
`f498e08bf978b36af750a3958c83b788d39f786069d31d6483d4df66d4ce69ba`）：
`0x7970..0x7998` 将 `0x2BBA8` 的 ClockBSP 装入上下文；其首指针为 `0x20280`、
数量为 8。`Clock_InitBases` 的 `0x3C80` 循环使用这张表，每项 24 字节，包含
物理基址、大小和映射结果指针。这给出了初始化所需的完整八个窗口，而不只是
已报错的下一个范围；所有八项还与同机原始 DALSys 的父窗口记录精确匹配。

| OEM 窗口 | 基址 | 大小 | r13 处理 |
| --- | --- | --- | --- |
| APSS_HM | 17000000 | 02000000 | 保留原映射 |
| CLK_CTL | 00100000 | 00200000 | GCC 扩为父窗口 |
| CORE_TOP_CSR | 01F00000 | 00100000 | 保留原映射 |
| GFX | 03D00000 | 00100000 | 保留 r12 父窗口 |
| IRIS | 0AA00000 | 00200000 | VIDEOCC 扩为父窗口 |
| MDSS | 0AE00000 | 00200000 | DISPCC 扩为父窗口 |
| PCIE_RSCC | 01D00000 | 00015000 | 新增明确列出的窗口 |
| TITAN_SS | 0AC00000 | 00200000 | CAMCC 扩为父窗口 |

r13 默认关闭的映射开关保持不变；开启后共追加六项 NS_DEVICE 区间，原始 41 项
和高内存试验布局不变。主机测试编译实际 C 函数的六种开关组合，校验两份原厂
PE 的 SHA、BSP/父窗口对应、FDT 子范围包含、八个窗口覆盖、对齐和无重叠；同时
校验应用内的诊断窗口与原始 ClockBSP 顺序和范围一致。结果全部通过。

实机检查从首尾页抽样改为对八个窗口全部 10,773 页执行 AT S1E1R/W，核对身份
映射、GCD MMIO 和 Device 属性，仅逐窗口显示汇总。它不读写外设寄存器；任何
失败仍会在执行 Clock 前停止。通过后才调用未修改的 ClockDxe；尚未加入 UFS、
磁盘日志写入或分区修改，硬件结果须另行记录。

r13 镜像 SHA256 为
`78f005f629f15bbb859852447222f2d8f6ddeb9a0528642241ade87d167bf204`。
两轮离线编译、FAT 文件读回和镜像结构检查通过，启动前 Android 基线不变；
Fastboot Sending/Booting 均返回 OKAY。实机逐页检查、Clock 返回状态和协议发布
结果仍待反馈；Booting OKAY 不代表 Clock 已成功初始化。

`clock-mapped-r13` 的 IMG_0686.HEIC 确认八个窗口全部 10,773 页检查通过，
fault/nonidentity/wrong-type 均为 0。Clock LoadImage 成功，用户确认长期停在
`starting ClockDxe NOW`，下方出现一条水平花屏。照片没有 Clock 返回状态或
前两轮的映射断言；不能把没有返回单独等同于硬件访问异常，也未确定花屏原因。
用户手动返回 Android 后五个启动分区、slot/build/vbmeta digest 均与基线一致。

接续 r14 使用显式 `--clock-trace`。保留原厂 ClockDxe 文件，在新的诊断副本中
仅将 23 条已核对的 BL、BLR X8、CBZ/CBNZ W0 改为专用 BRK：DriverInit、
InitBases、逐窗口 HWIO.Map、HALSetup、InternalAttach、SourcesSoft、Voltage、
DFSDomains、CESTA、Target、NPA 和后续 824c 调用。异常回调按原始指令恢复 PC
及 BL/BLR 的 LR，其余寄存器、栈、标志、函数返回值和原始条件分支均保留。
不跳过初始化或断言，不把失败强制改为成功；前 13 个原厂服务保持未修改。

该回调只接受自己的 BRK syndrome、指定 Clock 镜像内的 23 个精确位置和专用
指令字。未知异常继续原有报告/停机路径；无法取得自己的同步异常处理器则拒绝
启动诊断 Clock。实际 LoadImage 后也核对全部位置，原厂/诊断模式文件错配会
停止。上机首先运行 BRK→异常→RET 自检，成功才允许进入被跟踪的 Clock。

`tests/check-clock-trace.py` 将实际 C 控制流步进函数与 Unicorn 2.1.4 的 AArch64
原始指令执行对照：186 个输入/分支案例全部相同，包含正负偏移、W0/X0 宽度、
返回值高位、BL/BLR 的 LR、NZCV 保留；不支持的指令和空/不对齐 BLR 目标拒绝。
23 处补丁可完全逆转到原始 PE SHA，其他字节相同。模拟测试不代表实机异常入口
可恢复，也不证明 Clock 硬件初始化成功；跟踪还可能改变时序，故只用于定位。

r14 两轮编译、诊断 FAT 文件读回、BRK 自检桩机器码检查和镜像结构检查通过。
镜像 SHA256 为 `ec43fd5d3275acc80ada210e8bb2136c8ea23e09efa9dab63a4e980bf75fb3ef`，
元数据显式标记 `contains_oem_diagnostic_patches=true`。启动前 Android 基线一致，
Fastboot Sending/Booting OKAY；实机自检和最后一个跟踪阶段仍待反馈。

`clock-trace-r14` 的 IMG_0687.MOV（72.865 秒）记录了实际可恢复的跟踪：八个
HWIO.Map 和 InitBases 返回 0；InternalAttach、InitSourcesSoft、InitVoltage
返回 0；17 次 InitDFSDomains 也均返回 0。最后一条为
`HOUJI TRACE 61: InitCESTA BEGIN +07D3C`，没有对应 END。用户确认卡住，返回
Android 后五个启动分区及 slot/build/vbmeta digest 与基线一致。Trace 能执行和
恢复证明本轮自检门槛已经通过；尚不能把停止点具体归因于 CESTA 的锁、时钟等待
或寄存器访问。Clock 初始化和 UFS 仍未通过。

原 ClockBSP 的 CESTA 表位于 2BB98，两项为 cam_cesta（343F8，基址 0ADDA000）
与 pcie_cesta（34480，基址 01D05000）。cam_cesta 先依次开启
gcc_camera_ahb_clk、cam_cc_drv_xo_clk、cam_cc_drv_ahb_clk；pcie 项的前置时钟数为
0。这些范围包含在 r13 已检查的 OEM 父窗口内，不再为此增加新的 MMIO 映射。

r15 仍使用相同的显式诊断模式和指令步进算法，扩为 57 个跟踪位置，细分 CESTA
时钟查找/开启/释放、配置写入、供电准备、CmdDb/电压查询，以及 Clock 的同步锁、
源选择和 HAL 调用。轮询中的 delay/read 仅输出各自第一次命中，避免逐次打印
改变超时过多；原始指令仍逐次执行。查找时可显示已验证位于当前 PE 内的固定
长度 ASCII 时钟名，不读取任意指针或外设。390 个原始指令执行对照案例通过，
补丁可完全恢复为同一份原始 ClockDxe。硬件结果仍须另行记录。

r15 已通过两轮编译、FAT 文件读回、自检桩机器码和镜像结构检查。镜像 SHA256
为 `6fe1318404c00a8b4debcacfe73175947c0ab25b5e22c41748dacb769d516f53`。
启动前 Android 基线不变，Fastboot Sending/Booting OKAY；最后的 CESTA 内部阶段
及是否返回仍待实机反馈。

`clock-trace-r15` 的 IMG_0688.MOV（66.785 秒）继续进入了 CESTA 配置、供电
及释放前置时钟阶段。清晰尾帧的最后调用为
`TRACE 826: CESTA.DisableClock BEGIN +0A6C8`，前一条查找名为
`cam_cc_drv_xo_clk`，返回 ID 为 1005D。此前 cam_cc_drv_ahb_clk 已进入释放，
后续 XO 关闭未观察到返回；不能仅据此确定是锁、寄存器访问或关闭后的时序。
用户明确本次没有自动重启，是卡住后手动返回 Android。ADB 转由端口 5038 持有，
五个启动分区和 slot/build/vbmeta digest 与基线一致。

r16 为显式 `--clock-storage` 临时存储调试配置，保留相同的 57 处诊断跟踪。
在 LoadImage 已完成重定位、尚未 StartImage 时，验证 .data 中 ClockBSP 的
CESTA 表指针/数量以及两个节点地址；确认是当前固定版本后，仅把已加载镜像的
2BC50（8 字节指针）和 2BC58（4 字节数量）置零。原始 Clock PE 和磁盘内副本
的 BSP 数据不变，没有全局禁止时钟关闭或直接改写 MMIO。普通 profile 不改变
这些字段；任何布局不符在写入前拒绝。

这个配置有意暂不初始化相机、PCIe 的 CESTA，适用于继续验证 Clock/UFS 基础链，
并不是 CESTA 故障修复，也不代表相机/PCIe 可用。原 InitCESTA 的空表分支在
A710 跳到 A82C，正常返回 0；后续 Target/NPA 和真实协议安装仍须实际执行。
`tests/check-clock-storage-policy.py` 运行实际 C 策略，验证默认无改动、只改 12 个
可写数据字节、布局/数量/空指针/尺寸/重复操作拒绝且不产生额外写入；再以
Unicorn 执行原始 InitCESTA，确认空表路径 17 条 ARM 指令后返回 0，未进入
CESTA 配置/供电/释放调用。该离线结果不证明存储配置在实机成功。

r16 已通过两轮编译、FAT 读回和镜像结构检查，镜像 SHA256 为
`64946a7453888964cfb696f699abb1a62b38bfcf495109a00b6ec4abec78fd59`。
启动前基线不变，Fastboot Sending/Booting OKAY，元数据明确标记临时 BSP 策略。
Clock 的实机返回和协议发布仍待反馈。

`clock-storage-r16` 的 IMG_0690.MOV（71.05 秒）确认 TRACE 61/62 的 InitCESTA
进入并按空表策略返回，随后 InitTarget、InitNPA、DriverInit、PostInit_824c 全部
返回 0。中间结果页明确为 `ClockDxe returned Success`、
`storage scope r16 result=Success`、`Clock after test present (Success)`。
程序完成后故意保持结果页；画面静止不代表本轮仍卡在初始化。用户返回 Android
后五个启动分区和 slot/build/vbmeta digest 不变。这个通过结果仅限 Clock 的
存储调试配置与真实协议发布，不代表 CESTA、UFS I/O 或 iOS 已可用。

下一轮先测试 HALIOMMU 和 ICBDxe 前置服务，不包含 UFSDxe。同机 UFSDxe 含
ConfigStorPartitions、RPMB provisioning、black_box 日志写入等路径，不能仅凭
应用调用 ReadBlocks 就认定其整个初始化只读；写路径审计尚未完成，暂不执行。

HALIOMMU 的原 DEPEX 为十二项架构协议；其 51B8 LocateProtocol 另取
0389b776-625f-11eb-83be-c741a913de34，由当前 EnvDxeEnhanced 的 2C5C 安装。
增加实际存在检查后才允许 StartImage。HALIOMMU 在 151C 安装
54b6d3b4-5d33-4f91-8600-6c41d5deb19a，UFSDxe 的 20258 确实引用同一 GUID。
ICBDxe 在 1474 安装 5824f9de-17d2-4b1f-a5fe-5eb77fa53093，并注册后续事件；
服务返回及协议存在不能单独证明后续总线投票或 UFS 传输已可用。

r17 的 `storage-services` profile 将前十四个服务保持为原厂字节，追加原始
HALIOMMU、ICBDxe 并保留精确 DEPEX 与额外依赖检查。显式
`--storage-services` 使用无 BRK 补丁的原始 Clock PE，继续临时 CESTA 空表策略，
因此同时验证 r16 结果是否依赖跟踪时序。Clock 返回且协议真实存在后，才逐个
启动两个新服务；成功还须发布真实接口。异常报告附上当前驱动名/基址/PC/ESR/
FAR，完成页明确显示 TEST ENDED。未调用 UFS、块写入或分区配置路径。

`tests/check-storage-services.py` 验证前十四项的 manifest 与前一版原厂快照完全
相同、全部十六个 PE 哈希不变、两个提供者 GUID 与实际安装位置匹配、Env 协议
生产/消费 GUID 对应、DEPEX 不接受损坏或不支持的形式，且服务列表无 UFSDxe。

r17 两轮编译、FAT 全文件读回及镜像结构检查通过。镜像 SHA256 为
`fbfe1cab367867e42b29247fe2c82c902e9b879b23bbe275a0e92d22b81bef6f`。
Clock 文件 SHA 与原厂 `f498e08b...` 一致，元数据单独注明运行时 BSP 策略。
启动前 Android 基线不变，Fastboot Sending/Booting OKAY；无跟踪 Clock 及新两项
服务的返回/协议状态仍待实机反馈。此轮未启动 UFSDxe，也未提交块写入请求。

`storage-services-r17` 的 IMG_0693.HEIC 确认 HALIOMMU 与 ICBDxe 均返回
Success，真实协议在 StartImage 后及最终复查时都存在，最后为
`services result=Success`、`TEST ENDED`。这是完成后静止的结果页，
不是初始化挂住。本轮使用无 BRK 补丁的原始 Clock PE，仍启用临时 CESTA
空表策略；可以继续存储基础链，但不代表完整 Clock、UFS I/O、持久日志或 iOS
已可用。未包含或启动 UFSDxe；返回 Android 后的基线复核另行记录。

r17 返回 Android 后五个启动分区、slot/build/vbmeta digest 与基线一致。

r18 使用明确的 `--ufs-readonly` 诊断配置。在原始 UFS PE 的副本中，
4A9C 在普通 LUN 枚举后转到原函数已有的退出路径，省去 RPMB 打开、
监听器分配、通知和异步事件等后续入口。11834 的 LDR W8,[X1,#88] 改为
专用 BRK，必须先注册自己的异常处理器并通过实机 BRK 自检，才允许执行 UFS。
允许时精确恢复 LDR 的 W8 与下一 PC；拒绝时返回 int32 -EROFS，并经原始
1185C 退出代码恢复真实栈和寄存器。没有通过修改 Context.SP 模拟栈操作；
Mu 的异常返回汇编并不使用该字段恢复栈。另将异步提交 11B0C 改为直接返回
-EROFS，避免异步入口绕过同步命令门。

命令门仅允许 NOP、读取描述符/属性/flag、设置易失 fDeviceInit，以及明确列出的
SCSI 读取/查询和仅切换到 active 的电源命令。写描述符、写属性、其他 flag 修改、
写方向、RPMB LUN、写/擦除/格式化及未知命令全部拒绝，不伪装成功。
原厂 PE 保留，诊断副本的四个指令变更可完全逆转回原 SHA。应用只读取物理
LUN 的一个 4096 字节 GPT 头，校验 CRC，重复 DiskGUID 不计数，目标为六个。
StartImage Success 单独不等于存储读取成功。

新增默认关闭的 UFS MMIO 开关，覆盖同机 DAL 的 1D80000+40000 父窗口，
包含设备树全部 HCI/ICE/HWKM/MCQ 子范围；原始布局及 Clock 映射不变。
实际 C 命令策略的 2827 个案例通过；Unicorn 验证原函数的入口栈、LDR、
拒绝后的真实退出路径及异步 -EROFS 返回；12 个实际 C 映射组合均通过。
离线检查不代表 DMA、UFS 读盘、持久日志或 iOS 已验证。

r18 通过两阶段编译、FAT 全文件读回和整包结构检查。镜像 SHA256
`62fea32447662067cd8c433d4119001eaa47e7f71845a56194e82bab7176b349`，
受限 UFS 副本 SHA256 为 `3baf0ffe...`，原始 Clock 和其余十六项前置 PE
保持原始字节。启动前 Android 基线一致，Fastboot Sending/Booting OKAY。
实机 UFS 入口、命令门、GPT 读盘结果仍待反馈；本版不包含持久日志写入器。

`ufs-readonly-r18` 的 IMG_0702.HEIC 确认 UFS 映射 64 页、异常处理器和
BRK 自检通过。两次 NOP 均显示 ALLOW，随后 143/143/228，UFS.c:2544
Device Error 断言。用户确认断言后自动返回 Android。启动分区基线及六个
LUN 的主/备 GPT 原始备份范围全部一致。

更正 r18 的入口审计：11B0C 是同步、异步共同使用的完成等待函数，不是
单独的异步提交入口。102EC 的 NOP 在 10378 提交后，10388 必须调用它；
负数返回直接触发 103F4 的 143，外层 D078 两次失败后触发 D0E0 的 228。
r18 将该函数改为 -EROFS，正是本轮失败的原因，不能据此归因于硬件。
先前仅验证策略及短指令片段的测试没有覆盖完整生命周期，未发现这个错误。

r19 保留原始 11B0C..11C58 全部字节，不再修改完成等待路径。诊断副本只改
4A9C 的普通 LUN 后裁剪与 11834 的命令门，共两条指令。异步请求仍由命令门
检查 request[5] 拒绝，不通过破坏共用等待函数阻断。

新增实际 ARM 完整回归：D078 重试、102EC NOP、12618 请求布局、1180C 提交、
11B0C 等待、11718 响应解析、11C58 清理均执行原 PE 机器码。模拟控制器只提供
门铃/完成位和 NOP-IN DMA 响应；缓存、屏障、延时调用作环境模型，不替换驱动
返回值。原厂及 r19 完成模型均成功；以实际 r18 SHA 对应补丁重跑，精确复现
143/143/228 和 -EROFS；未完成模型产生超时，错误 NOP-IN 模型产生 144，均不
被转成成功。2827 个实际 C 策略案例仍通过。模拟不证明真实 DMA/时序已正常。

r19 两轮编译、FAT 文件读回及整包校验通过。镜像 SHA256 为
`4150774671f9e1d7c26c2d88f554f900f96f215b2cd78bb1c53984a1539c49a8`；
受限 UFS PE SHA256 `366896165157495de9d8e00698a23ec4bc8bf879752d7eeabebb07ca9dda9297`。
LoadImage 后还要求 11B0C/11B10 的原始指令，拒绝误用 r18 副本。
完整机器码回归与策略测试记录于 storage-candidate/ufs-readonly-r19/guard-test.txt。

r19 启动前五个引导分区和 slot/build/vbmeta digest 均与原基线一致，
Fastboot Sending/Booting 返回 OKAY。实机完成等待、UFS I/O、GPT 结果待反馈。

`ufs-readonly-r19` 的 IMG_0706.HEIC 显示 NOP 后已执行 SCSI TEST UNIT
READY，LUN D0，返回固定格式 Sense `70 00 02 ... 04 02`，继而 28b 和
UFS.c:2544 Device Error 断言。本轮不是 r18 的 143/228。T10 ASC-NUM
https://www.t10.org/lists/asc-num.txt 的 04/02 为 logical unit not ready,
initializing command required。启动分区和 slot/build/vbmeta digest 返回后
仍与原基线一致，GPT 读取尚未到达。

按用户要求，当前 UFS profile 去掉已通过基础链的全部观察停顿：启动前 15 秒、
Clock 的 15+2+8 秒、HALIOMMU 和 ICB 各 2+10 秒，共去掉 64 秒；新 UFS 入口
提示缩为 3 秒。驱动内部等待、超时和轮询不变。

r20 在 B008 的原 BL CA4C 位置使用专用 BRK 转到应用的正常调用上下文，
保留 BL 的原 LR，不在异常回调内运行驱动。验证原镜像的二十项 handle 表范围、
控制器指针、cookie、core 0、LUN D0 后，仍先调用原始 CA4C 就绪检查。
只在失败且控制器保存的 Sense 精确为 02/04/02 时，调用原始 CB78(Handle,1)
发送 SSU Active，再执行原始就绪检查一次。其他 Sense、Active 失败、再次就绪
失败均保留真实错误，不改写为成功，也不更改 controller 的跳过初始化标志。

实际 CB78 机器码构造的是 CDB `1b 00 00 00 10 ...`，Active 位在 CDB[4] 的
高四位；相应将命令门从先前未命中的 01 改为精确允许 10，仍拒绝 Sleep、
PowerDown、load/eject 和其他未知字段。新增测试执行实际 CB78 机器码核对 CDB，
编译实际重试 C 算法验证精确 Sense、调用顺序、单次重试和错误传递，验证 handle
边界及 BL link；3083 个命令策略案例和完整 NOP 回归通过。
此为 Active 状态恢复试验，实机是否能越过 TUR 尚待验证，不代表 UFS 读盘成功。

r20 两轮编译、FAT 文件读回和整包校验通过。镜像 SHA256
`601a4c094e914db8274c152f940bf1cd6e9c5bdb1ceace9d4b4d7d8b04020ea0`；
受限 UFS PE SHA256
`3f2c4f7b84a3e98415e0222f637116c2512d2ea9cf2023b2298455af9e37a354`。
已冻结源码及策略、完整 NOP 和 Ready/Active 回归测试输出；实际状态切换待反馈。

r20 启动前 Android 基线不变，Fastboot Sending/Booting 均 OKAY；实际
Active 命令返回、TUR 重试和 GPT 读盘结果仍待实机反馈。

## r20 实机 UFS 读取成功，r21 日志落盘准备

IMG_0709.HEIC 确认原厂 SSU Active 返回 0、再次就绪检查返回 0，随后六个
不同 LUN 的 4096 字节 GPT 头读取及 CRC 全部通过，最终 GPT read result=Success。
允许 43 条命令，拒绝 0 条；主 LUN 头 CRC 631fc7ea，其余为 9ce5a274、
4d68587b、764887b6、49bae3eb、2e0fc1e8。返回 Android 后五个引导分区、
slot/build/vbmeta digest 与原基线一致。首次 28b 已被后续真实 Active/Ready
恢复，不是最终失败。该结果不证明写盘、完整根文件系统或 iOS 已运行。

r21 开始前重新只读备份完整 logfs 8 MiB，设备读取前、本地与读取后 SHA256
均为 5c30e0e714a397b3780a7717f540bc68c231b0728a1bfc9c31d5fccc44022ab7。
新显式 --logfs-write-test profile 保留只读初始化，先验证主 GPT 头、完整
64 项分区表 CRC、磁盘与 logfs GUID，再非递归连接主 LUN 和唯一匹配的
logfs 子设备。仅在这些条件通过后开放物理 LUN0 的 [1000,1800) LBA
WRITE(10) 及无数据 cache flush。命令长度、同步标志、CDB 和范围均验证；
不允许 GPT、启动分区、其他 LUN、RPMB、擦除或格式化。原 --ufs-readonly
不开放此扩展。创建 UniXNU.log，已有同名文件时拒绝覆盖；Flush/Close 后
读回比较内容和 CRC，成功后重启 Android，另通过 ADB 独立验证。

首次覆盖范围是诊断应用自己的 Print 和 SerialPortWrite 文字，128 KiB
有界 RAM 快照，异常回调内仅追加内存、不执行文件 I/O；不是全局 OEM
DebugLib 或 XNU 消息缓冲。写入边界实际 C 的 100876 案例通过，并执行
五组原厂 ARM WRITE 构造器至实际命令门，验证分区起止、越界及 FUA 字段；
原只读 3083 案例、完整 NOP 生命周期、Ready/Active 回归继续通过。
r21 持久性结果在实际取回文件后另行记录。

r21 实机再次六 GPT PASS，pinned GPT/logfs entry matches=1；随后 parent
ConnectController 返回 Not Found，在开放写门之前停止，未创建 UniXNU.log，
没有自动重启。返回 Android 基线一致。审计发现此前把 DSC 中构建组件误认为
都已装入 FV：实际 RAM profile 中有 DiskIo/FAT，缺少 PartitionDxe。r22
在 GPT 校验后从嵌入 FAT 显式 LoadImage/StartImage 原 EDK PartitionDxe，
仅用于日志 profile；默认 stage-1 不变，并增加 parent DevicePath/DiskIo
诊断。FAT 20 个文件均读回相同。镜像 SHA256 为
57da47f9acab876daa23dcee95a13dc3b9c6bbb18bae5d4484bfa54f455f5fe7。

Android 会继续更新 UefiLog，编译期间 logfs 原始 SHA 再次变化，预检仍拒绝
过期备份。紧接启动前重新备份完整分区，前/本地/后哈希一致，并绑定到
logfs-backup-before-persistent-log-r22-final（SHA 5b29d41c460bf150634283512535afab570f1fc65b59d96b592ee70876f514f6）。
备份 SHA 是本地 provenance 元数据，更新它不改变固件/FAT/boot image 字节；
镜像 SHA 及文件清单仍验证。实际 r22 文件系统和写入结果待反馈。

r22 跨重启日志验证 PASS：回 Android 后只读取回新建 UniXNU.log，
11,609 字节、233 行、233 个 HOUJI 标记，r22 起始和最后 snapshot end
均完整。PartitionDxe/FAT/分区匹配全部 Success；设备前/本地/后 SHA
一致，为 643e6a4a69ffdb1c0813133b2cfae615bf1d6e25ffced8c9ef49f8d2eb8c092b。
返回后的五个引导分区、slot/build/vbmeta digest 以及六 LUN 主/备 GPT
共 12 个备份范围全部与原始基线相同。此为已验证的诊断应用文件持久化，
不代表完整 OEM 或 XNU 持续日志，也不代表 iOS 已进入。

## r23 UFS 到原生内核的交接状态

r23 在 OEM UFS 成功初始化、六 GPT 再次通过后，仅做 HCI/SMMU 读取。
完整扫描并自动返回 Android；通过 ADB 取回快照，payload 18,919 字节，
CRC32 66557540，长度和 CRC 在 Mac 独立重算一致。启动分区基线不变。
HCI CAP=f5c7031f、版本 0400、HCS=010f、HCE=1，UTRL=d2898000，
UTRL run=1，transfer/task doorbell 都为 0，IE=0。
SMMU IDR0=4c017e6b、IDR1=60000047、IDR2=5111，107 个 SMR、
71 个 context bank、128 个 global 页。只有两个有效 stream；
UFS SID60 位于 slot0，S2CR=0，指向旧 context0（SCTLR=0a9f00e0，
CBAR=0001f000，CBA2R=1）。本轮读取没有发生外部异常。

原 Ace 5 的接管策略在这份实测快照上会为 UFS 分配独占 context70，
保留旧 context0 和显示 stream 的映射；主机故障用例验证队列未停、
stream 改变、掩码冲突、地址宽度不足时拒绝，并在 bank70 被占时
改选 69。此时还没有在手机上执行这组原生接管写操作。

## r24 XNU 原生 GPT 读取测试准备

新增默认关闭的 houji_ufs_read=1，放在 BSD 早期 timer/USB/RTC 等待前，
调用直接编入 XNU 的原生读取代码。参考 Ace 5/NativeUfs 固定源码，
不沿用其 userdata、SHNGROOT 或系统卷布局，也不发布 IOBlockStorageDevice。
原生提交门只允许 NOP、读取电源属性、LUN0 TUR、Active-only SSU，以及
LUN0 LBA1/count1、LBA2/count2 的 READ(10)；数据出方向和写命令全部拒绝。
头和全部 64 项分区表分别须匹配本机备份 CRC 631fc7ea/9f1bb496。
成功和失败都以明确的 HOUJI NATIVE UFS READ 页面停止；这不是挂载 iOS。

1,288 个实际 C++ 命令策略案例、真实备份 GPT 和损坏输入检查通过；
SMMU 策略用实际 r23 寄存器输入验证。内核已编译链接，SHA256 为
b10bb1c267b7a06961ac386e64f4d2c93a2715053563039fa6ccb8dde4139558。
UEFI 先保存并关闭带 CRC 的日志检查点，关闭 logfs 写门，再通过实际
UniXNULoader v2 协议调用 DarwinBoot；只有显式 --xnu-ufs-read 才选择此路。
根系统卷不传入 RAM；此次只用 RAM 引导 UEFI、内核和 ADT。
已移除 UFS 入口原有 3 秒观察停顿；真实轮询和超时保留。
XNU 本身的结果仍不在 UEFI 检查点中，首次原生读盘结果需要屏幕反馈。

r24 两阶段固件构建、23 个载荷文件读回和整包校验通过。最终镜像 SHA256
fd83754cfa9eb1e06a34eb53e0e9932fd9c100150ca41f4f025b7be6cc88f307。
启动前再次归档上一轮已完整校验的日志，备份包含它的整个 logfs；仅删除
自己的旧 UniXNU.log 并 sync，再备份用于本轮的完整分区，SHA 为
840a950254d709adc53998dd3237f139a0517cdcce2f04a4ffaf4aeec12bc432。
旧日志和所有旧分区快照均保留。启动前五个引导分区及 slot/build/vbmeta
与原基线一致，Fastboot Sending/Booting OKAY。原生 XNU GPT 结果待屏幕
反馈；此时不能将固件和内核编译成功写成原生 DMA 或读盘已成功。


## r24 原生读盘实机通过

IMG_0714 的末页记录 HOUJI NATIVE UFS READ PASS：XNU 实际接管 UFS DMA，
完成 NOP、电源状态查询、LUN0 就绪及 GPT 主头和全部 64 项读取；CRC 分别为
631fc7ea / 9f1bb496，DMA=ba300000，独占 SMMU bank70，slot0。
此处 panic 是测试预设的停止页，不是新的读盘失败；未发布根介质或挂载 iOS。
返回 Android 后，五个启动分区及 slot/build/vbmeta 与基线一致，六个 LUN 的
主、备 GPT 共 12 个原始范围逐字节与备份相同。进入 XNU 前的 UniXNU.log
已独立取回，payload=11,557 字节、CRC=dd80495d；该快照不含原生内核结果。

## r25 原生内核日志准备

Android FAT 先新建并分配 64 KiB XnuLog.bin，完整 logfs 在分配前、后均保存。
实际 FAT12 链为 cluster69..84，对应 LUN0 LBA4170..4185。读取挂载 FAT 的
裸块设备发现普通缓存可能过期，因此新增固定只读 O_DIRECT helper，直接读取
两次相同，并将原始 FAT 数据块与挂载文件逐字节核对。没有在 Android 中裸写。

默认关闭的 houji_ufs_log=1 在 r24 GPT 校验后读取 FAT12 元数据，重新核对
BPB、双 FAT、目录、固定链、其他文件不重叠、初始化内容 CRC 和本轮唯一编号。
只有这些检查通过才开放上述 16 个文件数据块的 WRITE(10)，不开放 FAT 元数据
或其他文件/分区写入。正文先写并 flush，最后写带 CRC 的提交头并再次 flush，
逐块读回全部内容成功后才通过本机 FDT 指定的 PSCI SMC 返回 Android。
内核消息缓冲尾部和显式存储阶段文字都在正常 BSD 初始化上下文复制；不从 panic
处理器递归访问存储。早于原生 UFS 可用处的故障仍不能通过此路径保存。

25,601 个实际 C++ 命令边界用例通过；真实 FAT 分配及初始化文件接受，损坏/
交叉引用拒绝；34 个事务失败点均立即停止后续操作，提交顺序和读回损坏检查通过。
本轮持久化与自动返回尚待实机验证，不能据离线测试宣称日志已修复。


## r25 内核日志跨重启实机通过

镜像 03122c99f839a6f2ccee24e2561f5a1bd1774882d4a143b00fb4a4f150bb4e32
临时启动后自动返回 Android，ADB 在 5047 重新上线。没有依赖照片或视频，
独立取回 XnuLog.bin 并导出 XnuLog.txt：payload=10,583 字节，正文 CRC32
15753b53，头部 CRC32 4f9a592f，本轮编号 ce304c0b298341848bf514b7187fc451
完全匹配；64 KiB 文件 SHA256 为
0023d7651164db7ac6b97384ccdfe5e9a3b172beb0dad605bd2593823b96dbd4。
内容包含 XNU 22.6.0 从早期启动到 BSD、原生 DMA/SMMU/GPT PASS、FAT 验证
及提交前的内核消息和阶段记录。DMA=ba2e4000，slot0、bank70。

返回后两次 O_DIRECT 全分区读取相同，依据原 FAT 链重建文件与 ADB 取回文件
逐字节一致；16 个数据块归属不变、无其他文件交叉引用。五个引导分区与
slot/build/vbmeta 仍为原基线，六个 LUN 主、备 GPT 共 12 范围仍与原备份相同。
证据在 evidence/logfs-adb-capture-r25 和 evidence/native-log-r25-result.json；
完整事前/事后 logfs 均保留。r25-source 保存这次实际固件、内核源与主机脚本。

此结果确认正常检查点的 XNU 消息快照可跨重启取回，并未实现实时 USB 日志或
任意崩溃保存。当前文件保持已提交状态；同一镜像不会静默覆盖，重新测试需要先
归档、用新的运行编号分配/初始化并重新绑定源码与启动元数据。iOS 尚未启动。

## r26 日历、HFS 注册与恢复环境实机通过

原来的 IORTC 等待改为显式 houji_epoch 启动参数提供 UTC 种子，并使用实际
19.2 MHz 架构计时器累计时间。没有发布虚构 IORTC 服务；这不是硬件 RTC 驱动，
种子以构建时刻为准，尚不提供联网校时或关机走时。实际 XNU 日志证实越过
IOKitInitializeTime，并完成 hfs-627.100.6 注册。

固定文件原生日志连续提交三次成功，最终 13,047 字节，CRC32=b684045e。
XNU 用经过测试的 BCB 字段修改返回已验证的 OrangeFox root ADB，取回无需照片。
misc 保留字段与 A/B 数据原样保存，未对 userdata 发出写命令。证据：
native-port/evidence/logfs-recovery-r26、evidence/native-ios-r26-result.json。

恢复镜像是原 houji OrangeFox 的三份 ramdisk 文本修改，使用原镜像相同的
公开 AOSP AVB 测试密钥生成可验证 footer；只写 recovery_a。原 recovery_a/b、
boot/init_boot/vendor_boot/vbmeta、校准与通信等 37 个分区均已保存并核验备份。
恢复失败槽位时 set_active=a 与 mark-boot-successful 仅改变 sde 中 boot_a/b
槽位属性和两层 CRC；分区布局未变。主机后续预检固定审计后的两个完整 GPT
快照，其余十个范围仍固定原备份，不能接受任意 GPT 变化。

下一步把已校验的 12 GiB HFSX 系统卷安装到现有 userdata 的起始部分，使用
真实 UFS 回调提供 BSD 块设备接口。md0 名称来自通用 XNU 接口，数据不存放 RAM。
只在本机 GPT 与 HFSX 体积/块大小/卷编号都匹配后开放该 12 GiB 的写入范围；
尚未实机验证挂载或 launchd，不能把以上检查点写成 iOS 已启动。

## iOS 根卷已写入 userdata 并完成全卷直接读取校验

现有 userdata 分区起始 12 GiB 安装了用户提供的 20H392 iPhone X 系统卷，
未移动分区边界。Android 个人数据已按授权覆盖。Fastboot 45 批报告成功后，
全卷哈希检查发现最后 640 MiB 与源不符；128 MiB 分块确认前 11.375 GiB
完全相同。对照源文件，仅修复该尾部的零区与原始最后 4 KiB 备用卷头并 fsync。
随后固定只读 O_DIRECT helper 从手机读取完整 12 GiB，SHA256 精确匹配
c49c2349a088af4888e3fda1ad08e0593e7eba70516d8b1c8177e78c7ab55385。
证据：native-port/native-root-install-r1/install.json、tail-repair.jsonl 和分块哈希。
没有把 Fastboot 的成功状态单独当作系统卷完整性的证明；稀疏写入的具体缺失
原因仍未确定。当前只确认卷安装完整，尚未确认 XNU 根挂载或 iOS 用户态启动。

## r27 原生系统卷挂载实机通过

XNU 原生 UFS 注册 12 GiB 回调块设备，随后实际输出：
`hfs: mounted HoujiIOS16 on device b(1, 0)`。
HFS 还成功创建并写入 hotfile btree，证明本轮不仅识别卷头，还完成文件系统
元数据 I/O。正常检查点日志 18,324 字节、CRC32=5c7ec672，运行编号匹配，
之后自动返回恢复环境。证据在 native-port/evidence/logfs-recovery-r27。
本轮按设计停在根挂载后，尚未运行 launchd，不能据此宣称 iOS 界面已启动。

## r28 到达 launchd 的平台检查

GUI ADT 实机确认 AppleCLCD 匹配、真实 1200×2670 / stride4800 几何守卫通过，
1125×2436 逻辑画布居中。根卷日志回放和挂载成功，正常日志保存至 before launchd
exec，19,570 字节，CRC32=e60bdd73。随后照片 IMG_0715 记录 launchd CS_KILLED，
namespace9/code14。源码 reason.h 对应 OS_REASON_EXEC / WRONG_PLATFORM，
不是内存或 UFS 错误。该构建沿用上游 MacOSX SDK 的 SWEET 兼容路径，
kern_exec.c 在 iOS 平台且初始 exec 没有 spawn 平台属性时要求已有 DEVELOPMENT
开关 unentitled_ios_sim_launch；本轮未传。后续先补参数验证，保留签名页校验代码。
本轮没有 iOS 用户态运行成功的证据；五秒观察线程尚未完成第一次记录。

Ace 5 固定参考的 scripts/make-ios16-payload.py 和 DEVELOPMENT_STATUS.md
也记录相同 9/14 平台拒绝，并通过 unentitled_ios_sim_launch=1 解决。其启动参数
同时包含 launchd_unsecure_cache=1：原始任务缓存加载器在没有 AMFI IOService
时使用这个既有开发模式。r29 同步补齐这两个用户态兼容参数，以及已有的
sweetfb_hz=30 / sweetfb_noconsole=1 图形呈现参数。尚待实机结果，不据参考机
的成功记录认定 houji 已运行系统服务。

## r29 已运行原生 iOS 用户态，图形尚未呈现

补齐参考启动参数后，60 秒观察完成并自动回 OrangeFox。最终检查点 CRC、
运行编号及头部校验通过；日志显示 launchd[1]、SpringBoard[5]、backboardd[35]，
以及 logd、securityd、mobileactivation、runningboardd、迁移服务等持续运行。
backboardd 还实际打开 IOMobileFramebuffer 并调用其用户客户端方法。
这些是手机上原生 iOS 用户态的运行证据，仍不是 iOS 欢迎或主屏幕已呈现。
用户明确反馈整轮始终只有启动日志。最后快照填满 61,440 字节上限，保留内核
消息尾部和显式检查点；未把缺失的早期消息解释成没有执行。

证据：native-port/evidence/logfs-recovery-r29，payload CRC32=c5d82f2d，
头 CRC32=9c0d50d9。下一轮延长到五分钟，加入实际帧交换/呈现计数和已有
只读进程 PC/LR 采样，定位界面与迁移服务的等待位置。

## r30 首次 iOS 图形呈现与服务等待定位

用户实机看见 Apple 标志和进度条；尚未看见欢迎、设置或主屏幕。重启恢复后
正常检查点日志通过运行编号与双 CRC 校验，保留至约 281 秒、第 52/60 次采样。
画面实际提交计数从 485 增至 490，交换计数从 1037 增至 1045。说明该时段
仍有图形工作；不能把静止的进度条直接解释为 XNU 整体停止。

用同一 20H392 本地只读系统卷的 dyld cache 解析 PC/LR，SpringBoard 的主线程
停在 LaunchServices 的 _LSCopyServerStore，同期迁移插件等待其数据库 reseed
回复；backboardd 与 accountsd 等待 DataMigration，securityd 等待 Accounts。
这些是采样时的等待点，不是已经证明的根因。完整系统卷正通过固定 O_DIRECT
只读 helper 导出，保留当次文件系统状态供检查。

r30 没有按设计自动回恢复，用户手动返回。源码审计发现原采样器持 task lock
执行可缺页的用户栈读取；不能据此认定这就是 r30 未返回的原因。r31 将寄存器
快照留在锁内，持有 map 引用后在锁外读栈，并加入 lsd/installd/appstored、
实时 VM 计数、每个进程的采样起止标记，以及采样前检查点。进程清单缩小到
目标服务，减少 64 KiB 日志尾部被重复清单覆盖。源码在 native/userland-sampler，
可由 scripts/patch-userland-sampler.py 校验并复现；编译通过不等于实机已通过。

## 主机 UUID 接口补齐候选

同版本 ios-sweet 的 NOTES.md 记录缺少 IOPlatformUUID 会影响 CurrentHost 配置。
当前 houji ADT 已去掉参考机伪造的 unique-chip-id，原始 ARM 平台 UUID 生成路径
因此没有输入。补充 ARM64 SWEET 专用、显式 opt-in 的 houji,platform-uuid 回退：
从本机已验证 USB 序列号和 UniXNU 设备命名空间生成 UUIDv5，16 字节写入 ADT，
供 gethostuuid 与主机配置使用，不设置 Apple ECID 或激活状态。原有 Apple UUID
优先路径保持优先；缺失、长度错误、版本或 variant 错误时不发布回退值。

实际 C++ 方法在主机执行 56 个边界和优先级用例通过，内核集成编译通过。
设备树对比除新增 UUID 和新随机种子外完全相同。此时仍未启动该候选，不能
认定 UUID 已在手机发布或已经解决迁移。可复现来源在 native/platform-uuid，
应用器为 scripts/patch-platform-uuid.py，ADT 生成器增加 --platform-uuid。

### r31 / r32 and the next high-memory profile

r31 completed all 60 samples through uptime 321.76 seconds and automatically
returned to OrangeFox. SpringBoard and backboardd were present; the owner still
saw only the Apple logo/progress screen. At the final sample the 256 MiB kernel
window had 902 free 16 KiB pages. These counters show limited headroom, but do
not prove that memory caused the LaunchServices/migration waits.

r32 uses 320 MiB, a device-bound RFC 4122 UUIDv5 fallback, and 180 samples over
15 minutes. While it was running the owner reported a faster progress bar and
requested more RAM after this trial finishes. No interruption is planned.

The 2 GiB candidate uses only normal DRAM from the captured Linux map, within
`[0x8c0000000, 0xbea400000)`. SEC/PrePi still constructs its heap from the named
low `DXE_Heap`. The houji high-RAM build caps the actual Mu `FindFreePages`
search at `0xffffffff`, including preferred/default bins, pool allocations and
resource-promotion retries. Explicit `AllocateAddress` remains unchanged so
DarwinBoot can own the whole XNU window. This cap is compiled only into the
opt-in high-RAM profile. Ordinary allocation exhaustion must fail, never spill
into high RAM for an OEM driver.

The high-memory loader checks ordinary page/pool addresses and then writes,
flushes and reads every 64-bit word of its owned 2 GiB twice, using address-based
complementary patterns. The first native trial stops before mounting HFSX,
verifies native UFS/GPT and fixed-file checkpoint/BCB return, and only then may
be extended to launchd. Host tests compile the real allocation search and the
real pattern loop; hardware results must be recorded separately.

For a new phone filesystem snapshot, `scripts/snapshot-native-root.py` can clone
a verified earlier APFS-hosted 12 GiB snapshot and replace only changed 128 MiB
chunks. It independently indexes all 96 chunks before and after transfer and
checks the entire reconstructed image against the phone. The r31 snapshot took
105 seconds and passed all chunk checks. It performs no userdata writes.

The r32 committed snapshot was recovered successfully after a manual return.
It ends at sample 139/180, uptime 718.26 seconds, with 911 free 16 KiB pages,
8,502 framebuffer swaps and 3,980 physical presents. The platform UUID is
published. The owner saw progress advance to roughly one quarter. Automatic
return did not run; the last saved checkpoint does not identify the later
stopping instruction. This is not a completed 15-minute run or a full iOS boot.

r33 passed on hardware: the pinned loader's full-window memory test gated
entry into XNU, which reported physical base `0x8c0000000`, length
`2147483648`, and native UFS DMA at `0x8c2b00000`. GPT CRCs and the real HFSX
volume identity matched. A fresh 17,637-byte checkpoint passed CRC/run-ID checks
after automatic OrangeFox return, before mounting HFSX. Misc reserved bytes
864 through the end were unchanged. This verifies the high-memory transport
profile; it does not yet prove iOS userland stability at 2 GiB.

### r34 / r35: persistent migration and verified 8 GiB

r34 completed all 180 samples at uptime 947.39 seconds and automatically
returned to OrangeFox. The owner saw approximately three-quarter progress,
with no Hello/Setup screen. The last checkpoint recorded 24,234 swaps and
10,765 presents. A stable 12 GiB root snapshot was reconstructed from 28 changed
128 MiB chunks; all 96 output chunks match the phone before/after indexes.
Its SHA256 is `a1c8992adb77d942294fefc99e5d10f9c3cc698fa3c0b5cf96a91a4579df7c59`.

The complete iOS precautionary stackshot identifies CoreLocationMigrator as
waiting for over 10 minutes. At that snapshot, memoryPressure is false and
compressor size/compression/decompression counts are all zero. The wrapper
waits in CLInternalPerformMigration for a synchronous reply; locationd's
CLInternalServiceSilo waits on CLHarvestControllerSilo. These are observed
wait sites, not proof of the underlying cause. No migration-completion flags
or Setup/activation states were seeded.

r35 passed the same full-word two-pattern loader test with an 8 GiB window
`[0x8c0000000, 0xac0000000)`. The captured FDT and Linux RAM map cover this
entire interval, with no overlap in 43 checked reserved ranges. XNU reported
8,589,934,592 bytes and native UFS DMA at `0x8c4284000`. GPT/HFS identity reads,
fresh committed checkpoint and automatic recovery all passed before HFS mount.
Misc reserved bytes 864 through the end remained unchanged. This verifies
memory/storage transport, not 8 GiB iOS userland stability.

The opt-in CPU0 EPSS probe performs only reads. r35 read MIDR `0x410fd801`,
MPIDR `0x81000000`, and requested performance index 12, but rejected its timing
sample because the direct reference-counter read moved backwards. No CPU MHz
is inferred from that invalid sample or from the 19.2 MHz timer. The next
version uses XNU's ordered clock_get_uptime interface. CPU/GPU DVFS, voltage
control and thermal governors remain unimplemented.

The userland observer now accepts houji_trial_samples in the range 12..720,
at the existing five-second interval; absent/invalid arguments retain 180.
This permits an uninterrupted one-hour first-boot observation rather than
forcing return after 15 minutes. It does not bypass any migration plugin.


r36 was temporarily booted at 2026-10-24 18:54:34 +08:00 with the verified
8 GiB profile, a fresh log run ID, the ordered read-only CPU frequency probe,
and 720 planned five-second samples. The owner manually returned to recovery
after the Apple progress screen remained stalled. A valid checkpoint retains
sample 581/720 at uptime 2969.11 seconds, with 361,502 free 16 KiB pages,
50,951 swaps and 22,049 physical presents. This is a partial observation, not
a completed one-hour run or an iOS GUI boot. The early CPU frequency sample
has fallen outside the bounded log tail, so no MHz is claimed.

The latest complete stackshot contains 206 processes and explicitly reports
CoreLocationMigrator waiting for 42 minutes. Locationd's CLHarvestControllerSilo
is again waiting in the same MIG stub, image offset 0xc74f4. Memory pressure is
false; compressor size and compression/decompression counts are zero. The
reply endpoint remains unverified. No migration, Setup or activation completion
state was injected.

The r36 12 GiB snapshot was reconstructed from 38 changed chunks and independently
verified against all 96 before/after phone chunks. SHA256 is
`623169b6b77e938f01f6135d80e47b0b721c5f288a953ca862a69018df46856a`.
Misc reserved bytes 864 through the end are unchanged. The original image and
checkpoint are archived before preparing another run.

### r37: two-CPU precheck candidate

See [CPU scheduling references](cpu-scheduling.md). The prototype exposes only
the captured equal-capacity affinities 0 and 0x100, uses the existing Clutch
scheduler, starts CPU1 through PSCI/SMC with an explicit physical context,
and uses the captured 0x40000 GIC redistributor stride. No frequency or voltage
writes are added. The first test requires a CPU1-bound worker and real timer/IPI
counters, then stops before mounting HFS. Compilation and host checks have
passed; hardware outcome is recorded separately.


r37 reached CPU0 online and requested CPU1, then panicked with a displayed
return of 3288334339 (`0xc4000003`, the CPU_ON function ID). The owner photo
IMG_0723.HEIC and linked-kernel disassembly show the cause: the inline assembly
string used `dsb sy; smc #0`, and Darwin's assembler treated the semicolon as
a comment. The emitted function contains DSB but no SMC instruction. Thus r37
did not issue CPU_ON; this result says nothing about firmware CPU_ON support.
The native checkpoint is still the exact fresh uncommitted file, consistent
with stopping before native UFS initialization or HFS mounting. The owner
returned to OrangeFox; UEFI logs and the photo were archived.

The corrected call uses separate assembly lines. `tests/check-psci-assembly.py`
cross-compiles the actual call for Darwin arm64, requires both real opcodes,
and rejects a regression fixture using the old separator. Its `--kernel`
option additionally checks the linked SweetCPU function before any new boot.


r38 fixes the emitted PSCI instruction. IMG_0724.HEIC reports CPU_ON returned
0 and a panic on CPU1: console output from an interrupt-disabled context.
This confirms secondary execution reached XNU, but not scheduler/worker success.
The initCPU diagnostic IOLog was still in the masked-IRQ path. The next version
records MIDR, MPIDR and initial IRQ state without output, then prints those
fields from the normal startup helper after processor_start returns. The global
console policy remains unchanged. A linked-code check now rejects direct console
calls from initCPU; the actual r38 binary is a verified negative fixture.


### r39 / r40: separate secondary state and interrupt delivery

r39 passed the actual PSCI call and avoided console output in masked initCPU,
but timed out waiting for both IOCPU Running and the first SGI. The owner photo
IMG_0725.HEIC shows CPU_ON returned 0; it does not distinguish which condition
was missing. The phone automatically rebooted to OrangeFox. The complete UEFI
log was recovered and XnuLog.bin still matches its exact uncommitted initial
bytes. Misc bytes 864 through the end are unchanged. This was a panic reboot,
not a successful phase-3 log checkpoint.

r40 saves the initialized GIC redistributor and CPU-interface state to memory
without printing in the masked context. The CPU0 startup helper reports saved
MIDR/MPIDR, IOCPU state, timer/IPI counters and pending bits, sends one bounded
post-start SGI1 to CPU1, and retains the existing strict completion gate. The
diagnostics read only already-configured GIC registers. They do not enable
new interrupt sources or change CPU frequency/voltage. Host ASAN/UBSAN tests
cover all eight pending-register frame offsets and reject inactive, out-of-range
and overflow cases without register-memory writes. Final linked-code checks
verify a real SMC instruction and no direct console calls in initCPU or the
snapshot helper. The image was temporarily booted; hardware result is pending.

The r40 photo IMG_0726.HEIC instead shows a CPU0 data abort before attempting
CPU1. Exact linked disassembly resolves PC fffffe0007a75170 to the newly added
GICR_IGROUPR0 read in houji_smp_capture_gic. Host memory-bound tests did not
validate hardware accessibility. The next diagnostic removes all redistributor
MMIO reads; it retains CPU-interface system registers and actual IRQ counters.
No SMP hardware conclusion can be drawn from r40.


### r41: two physical A520 cores passed the precheck

The system-register-only diagnostic completed, returned automatically to
OrangeFox, and produced a valid committed XnuLog.bin (SHA256
4ccfcd0d924447d585751d9e173498ceec1a3bb9eecbf341c649641e7a8a4422).
Both CPUs report MIDR 410fd801, with MPIDRs 81000000 and 81000100.
The CPU1-bound worker produced all 30 heartbeats; timer counters were 609/114
and IPI counters 40/72 at the preflight gate. HFS was not mounted. Misc reserved
bytes remain unchanged. This confirms actual secondary execution, thread
scheduling and interrupts; it does not yet validate multicore iOS userland.

CPU0's ordered read-only EPSS measurement returned 1689595 kHz over approximately
100 ms (cycles 168960176, reference ticks 1920007 at 19.2 MHz). This is a short
busy average, not a fixed operating frequency, maximum frequency or working DVFS
governor. No frequency or voltage writes were added.

Compared with r39, the helper is bound to CPU0 and sends one post-start remote
SGI. This test does not isolate which change was necessary. The next short
userland trial retains both changes and the strict two-core preflight.


### r42: short two-CPU iOS userland / locationd observation

The candidate preserves r41's successful startup helper and remote SGI, exposes
the same two A520 affinities and 8 GiB, and runs 24 five-second inventory samples
before the recovery checkpoint. It enables the passive locationd request 1203 /
reply 1303 observer and includes locationd plus fairplayd.H2 in stack targets.
The 12 GiB root window permits ordinary iOS writes, as in earlier userland
trials; no migration-completion flags or replies are synthesized. The r36 full
snapshot remains the last archived root state before this trial. Hardware
outcome is pending.


The r42 two-CPU userland trial completed all 24 samples and returned to recovery
with a valid committed log. At 152.244331 seconds: initialized=2, worker_cpu=1,
beats=30, timer IRQs 118120/105933 and IPIs 44477/43674. Display counters reached
3265 swaps and 1363 presents; 371100 free 16 KiB pages remained. Locationd again
waited at image offset c74f4. The passive observer captured one request, zero
replies, kotype 0 and queue count 2, but no in-space receiver at send time. The
current evidence does not identify the service or establish why it fails.

The r42 12 GiB root snapshot reconstructs 24 changed chunks from r36, with stable
independent phone indices and every output chunk matching. Its SHA256 is
93e38ceb8d625af22a753f31874d321dc8e97785becbf34483e6d53b091c4bab.
A shadow-backed inspection mount contains no fairplayd crash report. Repeated
exec messages alone do not establish a fairplayd failure cause.

r43 extends the passive observer with the existing XNU trace's temporary-owner
lookup, opaque kmsg receipt/copyout result tracking and token clearing on free.
It also records selected services' normal exit status and reason namespace/code
into a separate bounded memory record, printed by the sampler outside locks.
No port rights, message headers, return values, exit reasons or migration state
are modified. Host sanitizers test ownership, receive/free lifetimes, status
propagation and unchanged input objects. Hardware outcome is pending.


### r43 / r44: service wait narrowed without changing IPC semantics

Both two-CPU trials completed 24 samples and returned valid persistent logs.
r43 recorded one locationd request, no reply, temporary-owner port state 1,
zero copyout attempts and no unreceived-message free. fairplayd.H2 exited 11
times with status 0 and no exit reason. This is not a crash diagnosis and does
not by itself identify the request's service.

r44 compared the live destination reference with host service-port slots under
the host lock (with no port or observer lock held). None matched at request
time. Its final scalar BSD trace contains six consecutive SYS_open returns of
ENOENT before a clean exit. File paths were not captured in that version; no
missing file or hardware failure is inferred yet.

r45 records only the last eight open pathnames (maximum 160 bytes each) for
fairplayd.H2, using bounded copyinstr after the original syscall returns and
before the observer spinlock. It does not read file contents, modify syscall
results, change port rights or write migration-completion state. Host tests cover
ring rollover and PID reset with unchanged input strings and returned status.
