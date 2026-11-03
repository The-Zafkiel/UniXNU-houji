# 小米 14 的 iOS26 准备分支

`codex/houji-ios26` 从 iOS16 现有源码快照 `287c653e` 开始。用户在 r307 后结束
iOS16 调试并要求转向小米 15 参考项目所用的 iOS26。当前阶段准备固定版本镜像和
迁移入口；尚未完成 iOS26 内核、用户空间组装或小米 14 实机启动。

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
