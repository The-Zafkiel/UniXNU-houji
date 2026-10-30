# 开发指南

## 环境与输入

已使用的主机环境是 Apple Silicon macOS、Xcode 27.1、Python 3.12 或更新版本、Clang，以及 Rust 1.98.0 的 `aarch64-apple-ios` 目标。UEFI 使用 Linux 容器构建。Mesa 还需要 Meson、Ninja 和对应 Python 构建依赖。

下列输入需在本地自行取得，不能提交到 Git：

- iPhone X 的 iOS 16.7.16 / 20H392 IPSW，以及从中提取的用户空间、设备树和所需文件。成员校验值见 `board.toml`。
- 对应的 XNU 构建工具、KDK 和 Xcode SDK；本仓库不重新分发 Apple 的二进制输入。
- 自有设备匹配版本的 Qualcomm / OEM 固件、UEFI 二进制依赖和 GPU 固件。
- 设备的 GPT、引导链、校准、通信和恢复分区备份。

本仓库保存的是实机开发源码，尚未验证在全新电脑上一次性构建全部组件。以下流程分别验证源码组装和组件，不应将组装成功等同于可刷写镜像。

## 离线检查

```bash
python3 tools/check-source-tree.py
python3 tools/run-host-checks.py
```

检查覆盖 GPU 的 12 个频点、负载与空闲降频策略、HFI 表一致性、CPU 交互升降频和 Turnip BO 复用回归。BO 检查会在同一线程交错条件下验证旧函数失败、修复函数通过；它不仿真 GPU 命令执行。

## 组装固定版本源码

先取得 `sources.lock.json` 中对应仓库及提交，再从一个已有 Git 仓库向**全新输出目录**组装：

```bash
python3 tools/prepare-sources.py kernel \
  --upstream /path/to/xnu --output .deps/kernel
python3 tools/prepare-sources.py bootloader \
  --upstream /path/to/DarwinBoot --output .deps/bootloader
python3 tools/prepare-sources.py uefi \
  --upstream /path/to/Mu-Silicium --output .deps/uefi
python3 tools/prepare-sources.py graphics/turnip \
  --upstream /path/to/mesa --output .deps/turnip
python3 tools/prepare-sources.py graphics/runtime \
  --upstream /path/to/metal2vulkan --output .deps/metal2vulkan
```

工具读取 Git 中的固定提交，先校验和应用补丁，再复制覆盖层，不改变输入仓库的工作文件。输出包含源码，不包含 Git 子模块。UEFI 子模块须按锁文件的 `uefi_submodules` 单独准备，并应用 `uefi/patches/` 中对应的子模块补丁。OEM 二进制、BootShim、ramdisk 和最终 FD 打包仍需要板级构建配置，不能直接使用其他机型的产物。

## 固件头文件

内核需要的 GMU、SQE、AQE 数据只保存长度和 SHA-256，见 `kernel/firmware-inputs.json`。从自有匹配固件取得原始文件后生成私有头文件：

```bash
python3 tools/make-firmware-headers.py \
  --gmu /path/to/gmu.bin --sqe /path/to/sqe.bin --aqe /path/to/aqe.bin \
  --output .local/firmware-headers
```

将生成的两个头文件放入本地组装树的 `iokit/Kernel/houji-ufs-root/`，不要复制到仓库的公开覆盖层。哈希不匹配时应重新检查厂商版本，不能为绕过检查随意改动校验值。

## XNU 构建

准备好内核树、KDK、CTF 工具及内核构建依赖后：

```bash
export HOUJI_XNU="$PWD/.deps/kernel"
export HOUJI_KDK=/path/to/KDK/Payload
export HOUJI_FAKEROOT=/path/to/darwin-build-tools
export HOUJI_BUILD_ROOT="$PWD/out/kernel"
bash tools/build-kernel.sh
```

入口沿用已使用的 `DEVELOPMENT ARM64 SWEET` 配置和 Darwin 22 参数。它不负责下载 SDK、创建系统卷、组装 UEFI 镜像或写手机。`tools/iig-darwin22.py` 是此旧内核配置使用的兼容入口。

## 图形组件

`graphics/bridge/Cargo.toml` 使用仓库中的前端源码和 `.deps/metal2vulkan`。构建还需要本地提供的 AIR 输入（原构建的 `NMB_FIXTURE_AIR` / `NMB_FC_AIR`）、对应 LLVM 工具和 Mesa 头文件。运行时标识必须随源码变化而更新，以免错误复用 shader 缓存。

Turnip 的 Darwin 后端与内核 `NativeAdrenoABI.h` 必须匹配；不要仅替换动态库而保留不兼容内核。Objective-C++ 前端使用 `graphics/include`、Mesa `include` 和 iOS SDK；现阶段参考 `archive/scripts/build-native-timing-metal-v16.py` 理解原组件构建参数，不能直接执行其中的本机路径占位符。尚未提供经过全新环境验证的一键图形构建器。

## 实机验证

测试需区分：UEFI 启动、XNU 启动、GPU 命令完成、渲染正确性、持续交互性能。频点请求成功、GPU fence 完成或软件 120 Hz 通知，均不能单独证明桌面达到目标帧率。

每轮保留源码标识、组件哈希、镜像与分区布局、运行 UUID、带校验的日志和用户观察。日志可在返回 OrangeFox 后提取；处于早期异常或硬件锁死时仍可能无法保存最后一次调用。恢复和保留策略见 [recovery.md](recovery.md)。
