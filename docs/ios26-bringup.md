# 小米 14 的 iOS26 迁移

`codex/houji-ios26` 从 iOS16 现有源码快照 `287c653e` 开始。用户在 r307 后结束
iOS16 调试并要求转向小米 15 参考项目所用的 iOS26。当前已经编译并链接 XNU12377 arm64e / HOUJI 16 KiB 内核和新 DarwinBoot。
首轮 RAM 静态 PID1 镜像已发送到手机，用户反馈停在 UEFI；没有内核/PID1 进入证据。
用户现已暂停实机测试，当前仅做代码和本地构建。完整系统、GPU 和旧功能仍需迁移与验证。

## 固定输入

机器可读清单为 [`targets/ios26.json`](../targets/ios26.json)。参考项目固定在
[`f1ae906f`](https://github.com/superboyyy/hakios-arm64e-dada/tree/f1ae906fcc43ba8f763663257724ba938fb820ab)。
其 [ARM64E 说明](https://github.com/superboyyy/hakios-arm64e-dada/blob/f1ae906fcc43ba8f763663257724ba938fb820ab/base/docs/ARM64E.md)
记录 iOS **26.6.2 / 23G90**，早期使用 iPhone18,2；实际
[dada 启动脚本](https://github.com/superboyyy/hakios-arm64e-dada/blob/f1ae906fcc43ba8f763663257724ba938fb820ab/base/tools/dada-ios.sh)
使用 `23G90__iPhone18,1`，本分支按这个用户空间供体准备。

| 输入 | 固定值 |
| --- | --- |
| IPSW | `iPhone18,1_26.6.2_23G90_Restore.ipsw` |
| 大小 | 11,518,964,040 字节 |
| SHA-256 | `0ca5a8e383df508b464951410e9e8d627ead8454e47b57c21d06a27d0d04598c` |
| 用户空间架构 | arm64e |
| 参考项目源码内核 | `xnu-12377.121.6` |
| 实际板级目标 | houji / SM8650 / Adreno 750 |

内核标签来自参考项目的源码构建入口，不表示它等于供体 IPSW 自带的 kernelcache
版本。镜像从清单中的 Apple CDN 地址取得，放在仓库外；镜像、解密密钥、SDK、
固件及生成的系统卷均不能提交。

## 离线校验与组件清单

```bash
python3 tools/inspect-ios26-image.py \
  --image /path/to/iPhone18,1_26.6.2_23G90_Restore.ipsw \
  --output /path/to/ios26-inventory --check-crc
```

该入口只读镜像，检查完整大小、SHA-256、SHA-1、BuildManifest 版本、构建号与产品，
并按每个 build identity 枚举实际存在的组件路径、长度和 ZIP CRC。`--check-crc`
额外读取所有 ZIP 成员校验 CRC。输出目录必须全新，位于仓库外或忽略的 `.local/`、
`out/` 内。它不挂载卷、不解密 AEA，也不连接设备。

已对下载的完整镜像执行上述校验，三个 build identity 的组件均存在，全部 ZIP CRC
通过。迁移输入固定为 `Customer Erase Install (IPSW)` / `v53ap`，ApBoardID `0x0C`、
ApChipID `0x8150`；校验入口同时检查该身份和下列路径：

| 组件 | 此镜像实际成员 |
| --- | --- |
| OS | `094-95416-090.dmg.aea` |
| Cryptex1,SystemOS | `094-97238-092.dmg.aea` |
| Cryptex1,AppOS | `094-96441-094.dmg` |
| KernelCache | `kernelcache.release.v53` |
| DeviceTree | `Firmware/all_flash/DeviceTree.v53ap.im4p` |

后续据此提取组件，不要使用 Recovery identity 的 OS，也不要沿用 iOS16 的 DMG
文件名或 iPhone18,2 的设备身份。参考项目的系统卷和 cryptex 使用
AEA，需在本地取得对应 FCS 密钥并使用 macOS `aea` 解密，再核对 arm64e dyld cache
及子缓存。解密和用户空间组装尚未纳入本分支构建入口。

## 迁移顺序和当前边界

1. 将参考项目的标准 PAC、arm64e 装载、chained fixup 与 XNU 12377 适配分离出来，
   保留其来源。先处理 XNU 8796 到 12377 的接口变化与构建依赖。
2. 将 houji 的 CPU/GIC、内存交接、UFS、触控、framebuffer、恢复计时及本机固件接口
   迁移到新内核。dada 是 SM8750 / Adreno 830；不能复制它的 MMIO、设备树、频率或固件。
3. 先验证新内核启动、只读文件系统和最小 arm64e 进程，再组装完整用户空间、系统
   cryptex 与 launchd。启动恢复路径验证完成后，才进入桌面测试。
4. 最后迁移并分别验证 NativeAdreno、Turnip、metal2vulkan 和 AdrenoMetal 的 ABI、
   渲染与显示路径。升级系统版本不构成紫色区域或动画卡顿已修复的证据。

当前 `board.toml`、`sources.lock.json` 的 `components` 以及 `tools/build-kernel.sh`
仍描述保留的 **iOS16 / XNU8796** 实现。它们不能用于宣称已经构建出 iOS26。
新目标由 `sources.lock.json` 的 `ios26_preparation` 显式关联；迁移完成前不将旧补丁
重标为新内核补丁。

iOS16 工作目录及暂存区保留，不继续启动测试或采集运行期日志。本阶段未向手机写入
iOS26 镜像。主机检查通过只证明保留源码与准备工具的检查结果，不证明 iOS26 实机启动、
GPU 正确性或帧率。


## 当前实现与验证范围

新入口为 `tools/prepare-ios26-sources.py`，输入固定在 `sources.lock.json` 的
`ios26` 节。它从 Apple XNU12377 和固定的 dada 补丁组装，按显式清单复用
SM8650/A750 驱动，随后应用新平台适配；不修改旧 iOS16 工作目录。
DarwinBoot 先应用旧版板级补丁，再应用 `bootloader/ios26`。

内核采用 arm64e、16 KiB CPU 页；启动器支持 Darwin 25 的 1024 字节命令行，
在真实内核上校验 format-7 chained fixups，先验证全部链再写入，生成每次启动的
PAC 密钥并验证硬件读回和签名指令。固件回调期间恢复固件密钥，退出固件后
交接内核密钥，副核入口也独立安装密钥。内核及主机输出不记录密钥。

HOUJI 平台保留自己的 GIC 地址/stride、禁止已知会 abort 的 redistributor
分组寄存器读取，并对唤醒/RWP 等待设置超时。已编译原生 UFS、A750 传输、
电池/ADSP、触摸、背光和 DVFS 单元；编译不代表它们在 iOS26 上已工作。
新 framebuffer 与原生 GPU 共享一个 IOAcceleratorES 类型，避免重复类定义。

首轮镜像由 `tools/build-ios26-first-entry.py`、`tools/make-ios26-adt.py` 和
`tools/make-ios26-first-entry.py` 产生，只运行单核内核和自编静态 arm64e PID1。
它使用 RAM root，不运行 StorageProbe，不请求 GPU/UFS，不改原系统卷，也不
启用运行期日志搜集器。定时返回 Recovery 的内核线程仅调用已验证为 SMC 的
PSCI reset，启动前必须读取并核实无 wipe 的 Recovery BCB。

内核实际链接成功且无未解析符号，UEFI 编译成功，真实内核静态重定位检查通过。
XNU 的配套 SDK 符号导出步骤仍报告参考工程缺少的 Apple 私有设备符号；这不是
完整 SDK 打包验证通过。主机测试不能证明 PAC 在手机上执行成功、进入桌面或帧率。

## 功能对齐清单

| 功能 | iOS26 当前状态 |
| --- | --- |
| arm64e 内核、PAC、启动参数和 RAM root | 已编译，首轮停在 UEFI；新修正版仅离线构建 |
| 单核/PSCI/GIC | 板级地址及已知规避已迁移，实机待验证 |
| UFS/持久系统卷 | 原生 UFS 入口与 HFS715 已链接；完整 iOS26 root 尚未组装/安装 |
| A750/Metal/Turnip | 内核呈现同步与 arm64e Rust bridge/std 已构建；完整 Metal/Turnip 接入待完成 |
| 多核、CPU/GPU DVFS、热控 | 策略保留，iOS26 启用与实机验证待完成 |
| 触摸、亮度、电池和 USB 充电状态 | 硬件适配已编译，完整系统服务待验证 |
| 状态栏、系统应用、键盘及旧用户空间兼容 | 键盘/状态栏库与供体应用注册工具已构建 arm64e；运行期和服务迁移待验证 |
| Wi-Fi、音频 | 未作为已实现功能；缺少 Wi-Fi 后端会明确返回不可用 |

本阶段不以编译通过、CPU framebuffer 拷贝计数或用户感觉代替 GPU/高刷证据。


## 首轮结果与当前离线修正

首轮 fastboot 确认接收并执行 RAM 启动镜像，但用户看到的始终是 UEFI 界面和
长倒计时。不能把 fastboot 的成功当成 XNU 已启动。没有采集运行期日志，具体
拒绝阶段仍未知；用户要求暂停实机测试后，没有再发起手机启动或安装。

菜单配置原有 `fallback_timeout = 120`。UniXNU 在启动项返回、又没有输入设备时
会使用它再次倒计时；它独立于内核的 90 秒恢复定时器。新最小配置改为首次 3 秒、
回退 5 秒，并用显式 `on_return = "reset"` / `return_timeout = 10`：保留失败画面，
显示错误码 10 秒后执行一次重启，不再反复重试。默认的普通 UniXNU 配置仍回菜单。
DarwinBoot 增加 Mach-O、ADT、内存分配/校验、PAC 等阶段的屏幕错误提示。最小 RAM
窗口改为 2 GiB，仍执行所有权和完整内存验证；完整系统的内存配置另行确定。

Mach-O、ADT 解析代码已提取为启动器/主机共同编译的同一份头文件；实际内核和
实际 ADT 及所有截断边界通过 ASan/UBSan。单次重启策略也通过完整 1..30 秒范围、
错误状态保留、无效参数和不重试检查。这些不替代手机验证。

HFS 改用 Apple `hfs-715.120.4`，固定在 `124b970f`，保留旧系统卷格式所需功能并
接回原生 UFS 根设备入口。SDK 的 TargetConditionals.h、vendor 固件和运行绑定头
均留在私有组装目录。`tools/build-ios26-kernel.sh` 明确构建内置驱动的独立内核，
不生成外部 kext SDK；原有 SDK 符号导出诊断不能因此视作已修复或已验证。

GPU 呈现从参考 RedwoodGPU fence 改接 houji 的 producer/current-tail fence。提交锁
覆盖缓存获取与 CPU 拷贝，挂起的帧保留引用并重试，不显示未完成的缓冲；释放锁
后才等待或回收。缓存范围必须落在保留的 IOSurface descriptor/map 内，逐 CPU 页
验证 Normal-WB/Normal-NC 属性，拒绝 Device/故障映射。HOUJI 不再额外发布参考用的
空 IOAcceleratorES，以免和实际 A750 provider 竞争。此修正只解决新平台适配缺口，
不是对旧紫色区域或高刷问题已经解决的声明。

arm64e Rust bridge 使用固定 nightly-2026-11-03、参考 PAC target 和重建的 std，
开启认证调用、认证返回、认证陷阱及 intrinsics。`sha2` 使用 Rust 实现以避免
cc-rs 把实验 ABI 当成 musl。实际归档代码检出 4259 个认证间接调用和 12791 个
认证返回（含 Apple 的 RETAB），不以 target JSON 声明代替机器码检查。
完整 Metal frontend 和 Turnip 的 arm64e 构建、系统根目录及 launchd26 服务组装、
触摸/亮度/电池/多核与应用运行期验证仍未完成。

## 本地构建入口

- `tools/prepare-ios26-sources.py kernel`：提供 Apple XNU、固定 hakios、固定 HFS
  的 Git checkout；可通过 `--target-conditionals` 提供本机 SDK 头文件。
- `tools/prepare-ios26-sources.py bootloader` 与 `boot_manager`：分别组装 DarwinBoot
  和带单次返回策略的 UniXNU；上游目录只读，输出必须是新目录。
- `tools/build-ios26-kernel.sh`：使用 `HOUJI_XNU`、`HOUJI_REFERENCE` 指定私有依赖。
- `tools/build-ios26-bridge.py`：本地 nightly prefix、作者提供的 AIR 和独立 Cargo cache；
  工具链分发版本/hash 与 PAC target 记录在 `sources.lock.json` 的 `ios26.rust`。
- `tools/build-userland-compat.py --profile ios26`：键盘与状态栏的 arm64e 库。
- `tools/build-system-apps.py --profile ios26 --system-root ...`：只读扫描供体实际应用，
  生成专用目录并构建注册工具，不复用 iOS16 固定目录，也不安装或注册。
