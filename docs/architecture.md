# 代码结构与运行路径

本仓库维护小米 14 的 Darwin 22 / iOS 16 路径。它与最新 UniXNU 的板级模块格式并非完全相同；不要将其他 SoC 的设备树、内存地址、频点或 MMIO 写入直接用于 houji。

## 系统组成

```text
Qualcomm bootloader
  └─ Mu-Silicium UEFI + houji board package
       └─ DarwinBoot：内存布局、ADT、固件和设备状态交接
            └─ XNU 8796.141.3 / SWEET
                 ├─ CPU、GIC、定时器、调频
                 ├─ UFS / HFSX、触控、framebuffer
                 └─ NativeAdreno 内核驱动
                      └─ Turnip Darwin → metal2vulkan → AdrenoMetal → iOS compositor
```

图形调用由 Metal 前端翻译到 Vulkan，再由 Turnip 生成 Adreno 命令；内核负责内存映射、提交、完成同步和设备状态。箭头表示依赖关系，不表示所有图形特性均已实现。

## 有效源码

- `kernel/patches/local.patch` 修改固定版本 XNU 原有文件；`kernel/overlay/` 提供新增驱动和兼容实现。
- `kernel/overlay/iokit/Kernel/houji-ufs-root/` 包含 UFS 及 GPU 运行路径，`Adreno740/` 子目录是该路径使用的 NativeAdreno 组件。目录沿用上游命名，实际目标是 A750。
- `kernel/overlay/iokit/Kernel/arm/` 包含 CPU、framebuffer、HID 与平台适配；既有汇编和调度器改动在内核补丁中。
- `uefi/overlay/` 是 Mu-Silicium 的板级覆盖层。主树、Mu_Basecore、Silicium-ACPI 的修改分别保存为补丁。
- `bootloader/` 对应 DarwinBoot 的固定版本补丁与新增头文件。
- `graphics/metal/` 是当前使用的 Objective-C++ / Rust 桥接源码；`graphics/include/` 保存共享 ABI、PM4 辅助代码及自有测试 shader。
- `graphics/runtime/overlay/` 是固定版本 metal2vulkan 的替换文件，不是完整依赖副本。
- `graphics/turnip/` 包含 Mesa 修改以及 Darwin 内核后端，含 BO 释放顺序修复。
- `userland/` 保存键盘几何等适配。显示驱动的当前有效版本以 `kernel/` 为准，用户空间目录内的显示调查副本供对照参考。

`sources.lock.json` 固定上游提交；`source-manifest.json` 固定本次发布的文件校验和。更新源码后应同步更新清单，不能只修改历史实验中的同名副本。

## 历史资料

`archive/native`、`archive/scripts`、`archive/tests` 和 `archive/docs` 保留早期实验及来源，便于了解寄存器、接口和故障的调查过程。其中脚本已将原开发者路径替换为占位符，含有旧镜像、分区布局和校验绑定，不能作为当前安装器直接运行。

当前启动基线对应开发镜像 r222。该标识用于关联本地证据，不是发行版本；没有全新环境完整构建和通用刷机流程的保证。
