<div align="center">

# UniXNU · houji

**小米 14 的原生 iOS 移植**

Snapdragon 8 Gen 3 · Adreno 750 · Darwin 22

[功能状态](#功能状态) · [开发指南](docs/development.md) · [代码结构](docs/architecture.md) · [问题反馈](https://github.com/lonzzi/UniXNU-houji/issues)

</div>

基于 [UniXNU](https://github.com/UniXNU/UniXNU) 生态，为小米 14（`houji`）适配 UEFI、XNU 内核、硬件驱动及 iOS 用户空间。目前已经能够进入 iOS 桌面，使用原生分辨率显示、触控、八核 CPU 和实验性的 Adreno 图形加速。

项目仍处于开发阶段。GPU 性能、颜色与部分应用的兼容性尚有问题，暂不适合作为日常系统，也没有面向普通用户的一键刷机包。

## 目标设备

| 项目 | 配置 |
| --- | --- |
| 机型 | Xiaomi 14 / 小米 14，`houji` |
| 已测试型号 | 23127PN0CC |
| SoC | Qualcomm SM8650，Snapdragon 8 Gen 3 |
| GPU | Adreno 750 |
| 显示 | 1200 × 2670，原生像素输出 |
| 测试系统 | iOS 16.7.16，20H392，iPhone X / `d22ap` 用户空间 |
| 内核 | Darwin 22，自行编译的 XNU 8796.141.3 |
| 引导 | Mu-Silicium UEFI → DarwinBoot → XNU |

## 功能状态

| 功能 | 状态 | 说明 |
| --- | --- | --- |
| UEFI 与系统启动 | 可用 | 已实机进入锁屏、桌面和设置 |
| CPU 多核 | 可用 | 八核启动，适配大小核分组与调度 |
| CPU / GPU 调频 | 实验性 | 使用厂商频点表；温度反馈和热保护尚未验证 |
| 显示与触控 | 可用 | 原生分辨率、全屏触控、返回与最近任务手势 |
| 系统键盘 | 可用 | 设置、主屏搜索、App 资源库的宽度适配 |
| UFS 系统卷 | 可用 | 当前开发配置使用 32 GiB HFSX 卷 |
| Adreno 图形加速 | 部分可用 | 已有真实命令提交、GPU 写回和桌面渲染；掉帧、紫色阴影、文字异常仍待修复 |
| 墙纸与照片 | 部分可用 | 墙纸内容可部分显示，预览及照片应用仍不稳定 |
| 日志与恢复 | 可用 | 保存带校验的检查点，返回 OrangeFox 后读取；早期硬件锁死仍可能需要手动重启 |
| Wi-Fi / 蓝牙 / 蜂窝 | 待适配 | 尚未完成可用性验证 |
| 声音 / 亮度 / 充电管理 | 待适配 | Recovery 中工作不代表 iOS 驱动已经可用 |
| Safari 等应用 | 待验证 | 依赖网络和图形兼容性继续完善 |

软件刷新通知已尝试提高到 120 Hz，**这不代表面板已在 120 Hz 扫描，也不代表桌面能达到 120 FPS**。当前优化重点是渲染正确性、提交开销和显示输出路径。

## 开发

仓库包含板级源码、固定版本的上游补丁、图形适配和离线检查工具。iOS 镜像、KDK / SDK、厂商固件及分区备份需要自行准备，均不随仓库分发。

```bash
git clone https://github.com/lonzzi/UniXNU-houji.git
cd UniXNU-houji
python3 tools/check-source-tree.py
python3 tools/run-host-checks.py
```

离线检查验证频点策略、HFI 数据和缓冲区回收的已知回归，不模拟手机或 GPU。源码组装、构建依赖及实机验证流程见 [开发指南](docs/development.md)。

## 代码导航

| 目录 | 内容 |
| --- | --- |
| [`kernel/`](kernel) | XNU 补丁、八核与调频、UFS、触控、显示和 GPU 内核实现 |
| [`uefi/`](uefi) | houji 板级覆盖层、BootShim 与 Mu_Basecore 补丁 |
| [`bootloader/`](bootloader) | DarwinBoot 的设备树、内存与 GPU 状态交接 |
| [`graphics/`](graphics) | AdrenoMetal 前端、metal2vulkan 适配、Turnip Darwin 后端 |
| [`userland/`](userland) | 键盘几何、导航与墙纸等用户空间适配 |
| [`tools/`](tools) / [`checks/`](checks) | 源码准备、构建入口与离线回归检查 |
| [`docs/`](docs) | 开发、架构、已知问题与恢复说明 |
| [`archive/`](archive) | 历史实验源码与调查资料，供追溯参考 |

当前依赖版本由 [`sources.lock.json`](sources.lock.json) 固定。修改应以各组件的有效覆盖层和补丁为准，历史实验目录不作为默认构建入口。

## 参与贡献

欢迎提交可复现的问题和小范围补丁。反馈请附上源码版本、测试镜像标识、预期行为与实际表现；日志中请移除设备标识及个人信息。提交规范和验证要求见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 致谢与许可

感谢 [UniXNU](https://github.com/UniXNU)、[Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium)、[Apple Open Source](https://github.com/apple-oss-distributions/xnu)、[Mesa](https://mesa3d.org/)、[AdrenoMetal](https://github.com/UniXNU/AdrenoMetal)、[metal2vulkan](https://github.com/steelbrain/metal2vulkan)，以及 [sheng](https://github.com/UniXNU/UniXNU-sheng)、[一加 Ace 5](https://github.com/tycykp/UniXNU-OnePlus-Ace5)、[小米 17 Pro](https://github.com/ShueiTsuki/data-a-live)、[小米 13 Ultra](https://github.com/markakash/UniXNU-ishtar-old) 等移植项目提供的实现和调查资料。

本仓库包含多种许可证的代码，保留各文件及上游原有条款。详见 [COPYING.md](COPYING.md)。
