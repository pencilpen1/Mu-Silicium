# lancelot USB Host 取数与适配记录

## 当前状态（2026-10-18）

- 用户报告 Mu 的 USB 遥测版本 `fastboot devices` 正常；此前 EP0 已到 configured。
  当前 Mu 版本的 bulk 命令双向通信仍需单独确认。
- 当前改动移除 `MusbDiagnosticDxe` 的底部字段和屏幕定时器，保留设备模式状态机及 RAM 日志。
- 固件仍为 USB Device Mode。Host 模式正在取数，尚未实现。
- 沿用已能进入 UEFI 的 lancelot 启动封装；BootShim 与 `Resources/Configs/lancelot.toml`
  维持现有基线。

## 已取得的 Android 证据

通过 Windows `adb.exe` 的无线 ADB 连接读取，KernelSU 提供 root；内核为
`4.19.325-cip136-st20-ShockwaveKernel-S+`，构建时间 `2026-09-25 07:39:18 UTC`。
原始记录位于 `/tmp/opencode/`：

- `lancelot-host-20261018-first.zip`：初始 Host 状态，仅根集线器。
- `lancelot-host-20261018-disconnected.zip`：用户拔下 USB 后。
- `lancelot-host-20261018-device.zip`：用户连接 PC 后。
- `lancelot-host-20261018-storage.zip`：用户确认 Android 识别 OTG U 盘后。

采集目录内的 `manifest.json` 记录命令、返回码、二进制校验和文件 SHA-256。
四次 role-before/after 快照各自一致；四份 FDT 一致，SHA-256：
`c68bdd716c1d70a134d6433169384cb004e46e91228facc9daa18433cc04a58f`。

| 项目 | 实测 |
| --- | --- |
| USB role switch | `/sys/class/usb_role/mt_usb-role-switch/role = host` |
| Type-C | data role = host，power role = source |
| extcon | `USB=0 / USB-HOST=1` |
| power supply | `/sys/class/power_supply/usb/usb_otg = 1` |
| Type-C 芯片 | 日志确认 WUSB3801，I2C `7-0060`，绑定 `usb_type_c` |
| 充电/OTG 芯片 | I2C `7-0055`，绑定 `smb1351-charger` |
| USB Host 控制器 | `mt_usb/musb-hdrc`，Linux `musb-hcd` 根集线器 |
| 外设 | 最后一组 `1-1` 为 Lenovo SX1 64G，`17ef:38ac`，`Generic / Mass Storage` |
| 外设驱动/速度 | `usb-storage`，480 Mbit/s，SCSI `sda` 及 `sda1/sda2` 已出现 |
| 时钟/GPIO/debug USB 摘要 | `CONFIG_DEBUG_FS` 未启用，路径缺失；regulator sysfs 已采集 |

| 物理状态 | role | extcon USB/HOST | UDC | usb_otg |
| --- | --- | --- | --- | --- |
| 拔线 | device（默认值） | 0/0 | not attached | 0 |
| 接 PC | device / sink | 1/0 | configured / high-speed | 0 |
| 接 OTG U 盘 | host / source | 0/1 | not attached | 1 |

U 盘描述符：EP0=64 bytes，interface `08/06/50`（Mass Storage / SCSI / BOT），
bulk IN=`0x81`、bulk OUT=`0x02`，max packet=512 bytes，配置声明功耗 200 mA。
采集显示根端口直接连接 `1-1`，没有枚举出独立多端口 USB Hub；外观上的转接器名称
不能替代 USB 数据拓扑。首组的根集线器速度不能用于证明外设成功，最后一组则有真实外设证据。

同次启动的旧日志还出现 GameSir-Tegenaria Lite (`3537:1094`) 的 Gamepad/Keyboard 接口，
伴随 `-71/-110/-19` 和反复断连；它不是本次稳定 U 盘快照的外设，后续键盘测试应单独取样。

启动后约 316 秒的日志记录了以下顺序：

```text
WUSB3801 attach
  -> extcon source vbus = 5000mv
  -> mtk_usb_extcon_set_vbus_v1(is_on=1)
  -> smb1351_enable_otg(en=1)
  -> role_sx_set(role=1)
  -> mtk-tphy set_mode_ext / power_on
  -> MUSB mailbox ID_GROUND
  -> mt_usb_host_connect / do_host_work
  -> musb_start(is_host=1)
```

这建立了本机 VBUS/角色切换的参考链。
`/sys/firmware/fdt` 和 `/proc/config.gz` 已保存为原始二进制，校验通过。

## 内核源码（用户已提供）

仓库：[MrShockWAVEog/shockwave_kernel_xiaomi_mt6768](https://github.com/MrShockWAVEog/shockwave_kernel_xiaomi_mt6768/tree/17.0)，
分支 `17.0`。本次固定参考 commit：`461f0c72b8ee70f4973dc2becd98c5801b0e31e0`。
这是采集时的分支 HEAD；Android 编译时的精确 commit 仍未由构建元数据确认。
29 个相关文件保存到 `/tmp/opencode/lancelot-host-source-461f0c72/`，其 `manifest.json`
记录每个文件的固定 commit URL 和 SHA-256。

已核对的关键路径（相对该内核根目录）：

| 文件 | 入口/发现 |
| --- | --- |
| `drivers/misc/mediatek/typec/tcpc/tcpc_wusb3801.c` | attach/detach 通过 `tcpci_source_vbus()` 通知 VBUS |
| `drivers/misc/mediatek/extcon/extcon-mtk-usb.c` | `mtk_usb_extcon_set_vbus_v1()` 调 primary charger 的 enable_otg / boost-current |
| `drivers/power/supply/mediatek/charger/smb1351-charger.c` | `smb1351_enable_otg()` 对 `CMD_CHG_REG` 的 `CMD_OTG_EN_BIT` 做 masked write |
| `drivers/misc/mediatek/usb20/mt6768/usb20_host.c` | `do_host_work()` 配 Host FIFO、开时钟，等待 VBUS，清 SESSION，PHY invalid，5 ms 后开 SESSION/Host PHY，再启动 MUSB |
| `drivers/misc/mediatek/usb20/musb_host.c` / `musb_virthub.c` | Host endpoint、传输和根集线器状态逻辑 |
| `drivers/phy/mediatek/phy-mtk-tphy.c` | 当前内核启用 `CONFIG_PHY_MTK_TPHY`，旧 MTK_MUSB_PHY 未启用 |
| `drivers/i2c/busses/i2c-mtk.c` / `.h` | 板级 I2C 传输参考 |
| `arch/arm64/boot/dts/mediatek/lancelot/cust.dtsi` | i2c7=400 kHz，WUSB3801 地址 0x60；实际 SMB1351 绑定以运行时证据为准 |

移植时需修正源码中的具体边界问题：extcon 传给 boost-current 的值为 500000 µA，
而 `smb1351_set_otg_current()` 仅在 >=1000000 / >=1800000 时赋值局部变量 `reg`，
小于 1000000 的路径使用未初始化值。UEFI 电流配置需要依据芯片位域重新核实，不能照搬该分支。

## 所需资料与收集进度

1. **内核源码链接、分支及固定参考 commit：已取得**（上一节）。
   若只能提供源码文件，请连同相关头文件、Kconfig、Makefile 和 DTS includes 提供：
   - `drivers/misc/mediatek/usb20/`：`musb_host.c`、`musb_virthub.c`、`musb_core.c`、
     MT6768 平台/OTG/PHY 文件；有些树使用 `drivers/usb/musb/`。
   - `drivers/phy/mediatek/phy-mtk-tphy.c`、`drivers/clk/mediatek/clk-mt6768.c`。
   - `mtk-extcon-usb`、WUSB3801、Type-C notifier/角色控制实现。
   - **实际绑定的 SMB1351 charger 驱动及其 enable_otg/boost-current 路径**。
   - MT6768 I2C 驱动、GPIO/pinctrl，以及 lancelot/MT6768 的相关 `.dts/.dtsi`、defconfig。
2. **OTG U 盘成功样本：已取得**。后续验证 UEFI 输入时再补普通有线键盘的型号和 HID 枚举；
   若要支持 Hub，请注明 Hub 型号和外接供电情况，并采集其实际 USB 拓扑。
3. **同一次 Android 启动的三组状态：已取得**。拔掉 USB（disconnected）、接 PC（device）、
   接 OTG U 盘（host）的角色、电源、USB 描述符和 dmesg 前后快照均已保存。

这些资料已足够开始 Host 控制器/板级供电适配。已有 ROM/LK/DTB/DTBO 和旧 runtime 资料
可继续使用；若 Android 内核/固件更换，再补对应版本。

## 采集方法

脚本：`Resources/Scripts/collect_lancelot_usb_host.py`。
Android 开启 USB 调试并授权 root。Host 采集通过无线 ADB 进行，使 USB 口能接外设。
无线调试端口可能在重启后变化，以 `adb devices -l` 为准。

每次确认物理连接已稳定，再执行一次；`--state` 是采集标签，实际角色另行保存：

```powershell
py -3 Resources\Scripts\collect_lancelot_usb_host.py --adb adb.exe --serial IP:PORT --state host --note "OTG adapter + keyboard model; Android works/does not work" --out lancelot-host-keyboard
```

将 `host` 分别换成 `device`、`disconnected`，并使用不同的 `--out`。每次生成一个目录和 ZIP，
包含 FDT、内核配置、dmesg、USB 拓扑/描述符、角色、I2C 绑定、供电、regulator、输入设备等。
脚本只读取设备状态，不强制切换角色或挂载 debugfs；缺失文件会标记为 partial/unavailable。
部分 `su` 会把错误写入 stdout 且返回 0，脚本也会识别这些命令错误。

## UEFI 适配顺序

1. 从匹配源码复核 WUSB3801 -> extcon -> SMB1351 VBUS -> MUSB Host 的寄存器与时序。
2. 衔接板级 I2C/Type-C/OTG 电源管理；Host 和现有 Device 驱动需要互斥拥有同一 MAC/PHY。
3. 实现 MUSB Host 根端口、电源/连接/复位、control 传输，再实现键盘所需 interrupt 传输，
   接入 `EFI_USB2_HC_PROTOCOL`。
4. 将现有 `UsbBusDxe` / `UsbKbDxe` 加入 lancelot 实际 FDF 并配置输入路径。
   它们目前出现在公共 DSC 中，但不在 lancelot 的 DXE/FV 列表；仅编译并不等于装入固件。
5. 验证键盘、热插拔、超时/STALL/toggle、退出 Boot Services；然后再推进 bulk/存储/Hub。

## 当前检查

```sh
python3 Resources/Scripts/test_lancelot_musb.py
python3 build_uefi.py -d lancelot -r RELEASE
```

回归脚本复用资料包的测试用例，但编译的是当前 Mu 的 MUSB/BootLog C 源码和 Mu_Basecore
头文件，覆盖 EP0 FIFO 交接、SET_ADDRESS、bulk、日志 backpressure/reset 和 ExitBootServices。
去屏幕字段版本的 ASan/UBSan 回归及 RELEASE 构建均通过；上机结果仍需单独记录。

成品位于 Mu-Silicium Git 根目录：`Mu-lancelot-usb-device-noscreen.img`（583680 bytes）。
这是去掉 USB 屏幕字段的 **Device Mode** 版本，仍沿用当前 boot v2 + gzip(FD) 封装。

```text
108c03b06ffd3a71c6fd47d339e9699995762bfe0b9661edb57e7f35a331a65f  Mu-lancelot-usb-device-noscreen.img
2a033ce90e34efc3b07497a26cbc0d9eb895fa83de7319bc73de9ad1a624440f  Mu-lancelot-usb-device-noscreen.fd
```
