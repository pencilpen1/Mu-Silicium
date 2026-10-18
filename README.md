# Project Silicium UEFI for ARM64-Based Devices

<!-- ![Banner](https://github.com/Project-Silicium/Mu-Silicium/blob/main/Resources/Pictures/Banner.png) -->

## Description

This Project focuses on Making ARM-Based Devices Boot Operating Systems like Windows or Linux using Custom UEFI Firmware.

## Resources

- [Status](Status.md)
- [Building](Building.md)
- [Guides](https://github.com/Project-Silicium/Guides/blob/main/README.md)
- [lancelot UEFI 移植指南](docs/lancelot_uefi_porting_guide.md)
- [lancelot 当前 USB 状态](docs/lancelot_usb_host_bringup.md#current-usb-status)
- [lancelot 独立 EDK2 历史资料包](docs/lancelot-porting-kit/README.md)
- [Discord Server](https://discord.gg/Dx2QgMx7Sv)

## lancelot / Redmi 9

`Platforms/Xiaomi/lancelotPkg` is the active MT6768 platform port. Its display
configuration uses the Android runtime framebuffer (`0x7cdb0000`, visible
`1080x2340`, stride `1088`) and the GOP opaque-pixel workaround documented in
the porting guide. The bundled porting kit contains the validated MUSB device
mode diagnostic driver, runtime captures, tests, and migration notes. lancelot
now defaults to the experimental [MUSB dual-role profile](Silicon/MediaTek/MediaTekPkg/Drivers/MusbHostDxe/README.md):
PC fastboot Device mode and OTG Host, with exclusive controller handoff.

Current USB status (2026-10-18):
- **Hardware feedback:** USB keyboard controls UEFI; Host/Device switching
  recovered on v0.3; `fastboot devices` discovery was reported working.
- **Partial support:** the current Mu image's getvar/USB-log transfers, USB-drive
  enumeration/access and broader hotplug stability still need hardware validation.
  The successful Android USB-drive capture is a hardware reference, not a UEFI result.
- **Latest fixes:** `v0.3-inputfix` corrects GPIO93 phantom down-key reads and
  renders USB status in place. Builds and host regression tests pass; phone
  confirmation of these two changes is pending.
- **Not implemented:** USB PD negotiation, high-speed hub split transactions,
  isochronous transfers, Device download/flash/reboot, ADB and gadget mass storage.

The default is `LANCELOT_USB_HOST=2` (dual role); `0` retains the standalone
Device profile and `1` selects Host only. See the
[current USB summary and validation record](docs/lancelot_usb_host_bringup.md).

## Frequently asked Questions

### **Is my Device Supported in this Project?**
- To Check if your Device is Supported, Take a Look at the Status Page Referenced in the Section Above.

### **What do I do if my Device isn't Supported in this Project?**
- We made some Guides that Explain How to Port your Device that are Referenced in the Section Above.

## Credits

[Microsoft](https://github.com/microsoft/) for their [Project Mu](https://microsoft.github.io/mu/) which Powers this Project. <br>
[WOA-Project](https://github.com/WOA-Project/) for their [mu_andromeda_platforms](https://github.com/WOA-Project/mu_andromeda_platforms/) which is used as Base for this Project. <br>
[Project Aloha](https://github.com/Project-Aloha/) for their [mu_aloha_platforms](https://github.com/Project-Aloha/mu_aloha_platforms/) which is used as Reference for some SoCs. <br>
[Open Surface RT](https://openrt.gitbook.io/open-surfacert/) for their Help on Tegra Related Stuff.

[sonic011gamer](https://github.com/sonic011gamer/) and [halal-beef](https://github.com/halal-beef/) for their Amazing Work on Exynos Related Stuff.

## License

All code except drivers in `GPLDrivers` & `GPLApplications` & `GPLLibrary` directories are licensed under BSD-2-Clause. <br />
GPL Drivers are licensed under `GPLv2` or later license.
