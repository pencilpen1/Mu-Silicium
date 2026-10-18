# lancelot UEFI 移植指南

当前 Mu 移植进度入口：

- [当前 USB 功能状态与修复记录](lancelot_usb_host_bringup.md#current-usb-status)
- [总状态页中的 Redmi 9](../Status.md#xiaomi-redmi-9)
- [MUSB Host / 双角色实现](../Silicon/MediaTek/MediaTekPkg/Drivers/MusbHostDxe/README.md)
- [MUSB Device / fastboot 实现](../Silicon/MediaTek/MediaTekPkg/Drivers/MusbDiagnosticDxe/README.md)

截至 2026-10-18：USB 键盘控制 UEFI、Host/Device 切换恢复和 fastboot 设备发现已有用户
实机反馈。U 盘在 UEFI 中的枚举/访问、当前 Mu getvar/日志传输以及复杂热插拔仍待验证。
最新 `v0.3-inputfix` 已完成 GPIO93 误报方向下修正并移除底部 USB 测试字符，构建/宿主测试
通过，实机复测待反馈。默认 `LANCELOT_USB_HOST=2`，独立 Device/Host 配置分别为 `0`/`1`。

硬件参数与独立 EDK2 的历史成果：

- [原始参数与迁移清单](lancelot-porting-kit/docs/lancelot_uefi_porting_guide.md)
- [原始资料包说明](lancelot-porting-kit/README.md)

`lancelot-porting-kit/` 是有 `MANIFEST.sha256` 校验的导入快照。其中的“当前”、
`ws/` 路径及 EDK2 USB v0.2/v0.3 验证结论均属于原独立工作区，不能代替上方的 Mu 当前状态。

当前仓库的可构建平台入口是
`Platforms/Xiaomi/lancelotPkg/`；资料包中的 `ws/` 是独立 EDK2 移植工作区，
用于保留 USB Device Mode、显示修复和启动内存布局的参考实现，尚未作为第二套
平台目录直接参与本仓库构建。
