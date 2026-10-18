# lancelot UEFI 移植指南

完整指南及其配套证据、源码、测试位于：

- [完整指南](lancelot-porting-kit/docs/lancelot_uefi_porting_guide.md)
- [资料包说明](lancelot-porting-kit/README.md)
- [当前 Mu USB Host 取数与适配记录](lancelot_usb_host_bringup.md)

当前仓库的可构建平台入口是
`Platforms/Xiaomi/lancelotPkg/`；资料包中的 `ws/` 是独立 EDK2 移植工作区，
用于保留 USB Device Mode、显示修复和启动内存布局的参考实现，尚未作为第二套
平台目录直接参与本仓库构建。
