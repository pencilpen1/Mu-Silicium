# lancelot USB Host 取数与适配记录

## 当前 v0.3-inputfix：原位状态刷新与持续方向下修正

用户确认键盘可控制 UEFI，但 F1 菜单持续向下，拔掉 USB 键盘后仍存在；同时要求
USB HOST/DEVICE 状态只在原位置更新，而不是清屏覆盖。

本轮改动：

- USB 状态改为独立 `UsbStatus.c` 增量绘制，取消整行蓝色背景填充和周期性内容轮换。
  只写入实际变化的像素；文字缩短/移除时恢复字形下方背景；若菜单已重画对应像素，
  采用新背景。未变化的内容不重复写屏或 flush；行外区域和 stride padding 保持原样。
- 修正 MediaTek `GpioGetState()` 的输入/输出寄存器选择错误：DIR=0 应读 DIN，DIR=1
  读 DOUT，原来恰好反转。lancelot 音量下键为 GPIO93 active-low；未按时 DIN=1、
  无关的 DOUT=0 会被旧代码读成一直按下。宿主测试编译实际 GPIO + keypad 源码，
  在旧代码上复现持续 SCAN_DOWN；修正后空闲、短按、长按与释放均通过。

这是确定的寄存器选择缺陷及其软件复现；新镜像上机仍需确认是否消除用户的全部误报。
正常音量键和 USB 键盘输入没有被屏蔽，Host/Device 状态机沿用已恢复切换的版本。

测试成品（Git 根目录）：`Mu-lancelot-usb-dual-v0.3-inputfix.img`，610304 bytes。

```text
013500721bdceec8397dcb01e7f624987247188ed082c4bbd1a8308d99010da2  Mu-lancelot-usb-dual-v0.3-inputfix.img
5f575a133ceab0ee609a3dc9a79b37db34be66d120e0dddd6c61db958b8d1c53  Mu-lancelot-usb-dual-v0.3-inputfix.fd
```

RELEASE 构建、双角色镜像检查、Host/Device/板级/待机/增量显示/GPIO-keypad 的 ASan/UBSan
回归均通过。重点复测：不接键盘时菜单是否静止；机身音量下短按/长按/松开；USB 键盘
上下键与 F1/F2；角色切换和底部状态更新是否仍覆盖原有界面。

## 当前 UEFI USB 0.3：角色判断和无系统待机修正

### v0.3 反馈与安静版

用户报告角色切换恢复正常，但待机持续打印 `Key received: scan=0002 char=0000`。
`0002` 是 UEFI SCAN_DOWN；ConIn 同时可能有 USB 键盘或机身按键，当前不能仅凭扫描码
认定重复输入来自哪个设备。安静版移除普通按键逐条打印及相关提示，保留 F1/F2 操作、
待机事件等待与底部 USB 状态。此次仅解决刷屏，不宣称消除了重复按键的来源。

安静版：`Mu-lancelot-usb-dual-v0.3-quiet.img`（608256 bytes）。待机重复方向键回归、
RELEASE 构建及双角色 FV/镜像载荷检查通过。

```text
05af959de32f190f3e8c4eb6c2bc263565eb09b5bd833022d8b41c8c19dab502  Mu-lancelot-usb-dual-v0.3-quiet.img
0ea9d9a9b68c9385343e58a368ac0b55ff4d48be1639ff16d462c0e311d87523  Mu-lancelot-usb-dual-v0.3-quiet.fd
```

### v0.2 的实机回归

用户报告 v0.2 无法正常读取 Host 外设，也无法切回 fastboot，底部停在
`USB HOST: WAITING FOR OTG`。没有收到当时 Type-C 原始寄存器值和持续计数；
不能将此文字视作已成功进入 Host，更不能认定 PHY/供电或 Type-C 中的某一个为唯一根因。

### v0.3 处理

- 修正角色判定：以当前 STATUS 类型为准；历史 INTERRUPT DETACH 不再每次重置
  稳定等待或反复停止工作中的传输。新角色稳定 50 ms、拔线稳定 150 ms 后确认；
  Host 观察到 PC/source 时仍立即关闭自身 VBUS。`0xff` 的无效 Type-C 读数返回错误。
- 在 lancelot 的 `DeviceBootManagerUnableToBoot()` 中保留 TPL_APPLICATION 待机，
  避免该 hook 返回后进入公共 BDS 的“无系统则等待 10 秒后关机”路径。
  这一关机路径确实存在，但尚未证明用户上一次故障正由它触发。
  UEFI 成功启动 OS 的路径与 BootShim/镜像封装没有变化。
- 原 v0.3 待机界面显示按键的 scan/char，安静版已移除；**F1** 打开现有 Boot Manager，**F2** 重新连接/扫描设备。
  没有输入设备时仍周期等待事件，维持 USB 轮询和角色交接；不要求先有可启动系统。
- 等待和错误状态新增 `C/I/A/W/D/T`，Device 文字改为 `FASTBOOT ACTIVE`，
  表示驱动已激活，不代表 PC 已枚举或 getvar 已通过。

字段解释：`C` 为 WUSB3801 STATUS（十六进制），`I` 为该次 INTERRUPT，
`A` 为当前 owner，`W` 为待稳定角色（`0=none, 1=Device, 2=Host`），
`D=1` 表示 Device 驱动已注册，`T` 为持续递增的角色/Host 回调计数。
例如 `C & 0x1c == 0x04` 为接 OTG/sink，`==0x08` 为接 PC/source。
错误状态与 Device 状态会轮换显示这些字段。

### 成品与验证

Git 根目录的 `Mu-lancelot-usb-dual-v0.3.img`（608256 bytes）及对应 2 MiB FD：

```text
baa9d68413719988429329d37708133d371e9a56f9675e3df4daf741fd55b8af  Mu-lancelot-usb-dual-v0.3.img
2869f74e8c3ab235a39bd3773d9a9ba6550351b2f8100fb8e5f8b4ce9b9fb36d  Mu-lancelot-usb-dual-v0.3.fd
```

RELEASE 构建、双角色固件卷/boot v2 载荷检查、Host/独立 Device/managed Device
ASan/UBSan 回归通过。增加了重复 DETACH、瞬态空角色、完整拔线再接 PC、无系统待机
超过原 10 秒阈值、F1/F2 和定时器创建失败回退的测试。**实机修复效果待验证**。

请先在启动后等待 15 秒以上，观察待机界面和 `T` 是否继续更新；再接 PC 测
`fastboot devices` / `fastboot getvar product`，随后换 OTG 键盘。若仍停在 WAITING，
反馈两行 `C/I/A/W/D/T`（最好两张间隔 2 秒的照片），即可区分当前角色读数、
Device 未注册、等待稳定及事件循环停止等情况。

## UEFI USB 0.2：热插拔与双角色兼容（历史测试版）

本轮成品（位于 Git 根目录）：

- `Mu-lancelot-usb-dual-v0.2.img`：608256 bytes，默认双角色测试版。
- `Mu-lancelot-usb-dual-v0.2.fd`：2097152 bytes，原始 UEFI FD。
- `Mu-lancelot-usb-device-v0.2-fallback.img`：583680 bytes，独立 Device 回退版。

```text
c2f4abb77d9b2fb947741a4c409d0211ed94ddd7c0acd15efc8ceb80da9aec4e  Mu-lancelot-usb-dual-v0.2.img
cf22a9fbae651865cac941f9b528a6c984caee1343bda61a792b4d6582f06a80  Mu-lancelot-usb-dual-v0.2.fd
c6e09695b70e04956067a489b7ab76bea4121c8e9104682d581212637308e0cf  Mu-lancelot-usb-device-v0.2-fallback.img
```

两个配置均已编译并分别检查固件卷的驱动组合；IMG 解压载荷与对应 FD 一致。
封装沿用 boot v2 + gzip(FD)，BootShim/启动地址保持当前基线。

双角色使用通常的 `python3 build_uefi.py -d lancelot -r RELEASE` 构建。
独立 Device 配置也可在已配置的环境中以
`python3 Platforms/Xiaomi/lancelotPkg/DeviceBuild.py TARGET=RELEASE ENABLE_SECUREBOOT=0 FD_BASE=0x40080000 FD_SIZE=0x200000 FD_BLOCKS=0x200 'BLD_*_LANCELOT_USB_HOST=0'`
直接构建 FD；此命令本身不封装 IMG，本轮回退 IMG 随后使用既有封装函数生成。

### 0.1 实机反馈

- U 盘停在 `POWER ON / WAITING DEVICE`。只拔 U 盘、保留转接头，再插键盘未识别。
- 重启后键盘被枚举为 `0c45:760b / ADDR=1 / LOW SPEED`，显示 `INPUT RECEIVED`。
  该旧标记只要求收到一次报告，可能是空闲报告，不能据此确认按键可操作菜单。
- 用户当前没有可操作菜单，Caps/Num Lock 后灯未变化。初始锁定灯默认全灭属于 UsbKbDxe
  的正常设置，但按键后的 LED/输入链路仍需新遥测区分。

### 0.2 改动

1. 同时处理 DISCONNECT/CONNECT；用稳定的 OPSTATE/DEVCTL 样本补查丢失事件，避免将
   高速设备没有 LSDEV/FSDEV 位误判为拔出。复位期间屏蔽瞬态，复位后保留消抖窗口。
2. 在 VBUS 上电前清旧事件；保留转接头时，若硬件 SESSION 已停止则限频重新启动。
3. 异步 IN 请求闲置但未返回报告时重新请求，保留接收状态位和 toggle。
4. 增加实际键码、LED SET_REPORT 成功/尝试次数及轮询计数，帮助没有菜单时测试。
5. 默认 `LANCELOT_USB_HOST=2`：PC/source 选择原 Device/fastboot，OTG/sink 选择 Host。
   停止旧 owner 后再激活另一方，Host 的端口查询、取消和复位在 Device 活动时不访问 MAC。
   Host 板级初始化失败时可回退至原被动 Device 路径；日志在角色切换中保留。
   配置 `0` 仍可独立构建原 Device，配置 `1` 为 Host 专用。

Host 与 Device 的回归测试均通过，包括实际 Device start/stop 后再次 getvar；
这不是本次双角色版本已实机通过的声明。上机需验证 PC -> OTG -> PC 及转接头后的换设备。

### 底部字段与复测

- 未枚举设备：`D`=DEVCTL，`O`=OPSTATE，`P`=POWER，`E`=最近非零 USB 事件。
- 键盘轮换显示：`R`=收到的有效报告数，`K`=最后实际 HID usage（十六进制），
  `L=成功次数/请求次数:LED位图`。Caps Lock 为 `39`，Num Lock 为 `53`，Enter 为 `28`；
  LED 位图 `1/2/4` 对应 Num/Caps/Scroll。
- `T`=轮询回调计数，应该持续增加。若卡住，可能是事件循环停止，不能直接认定是外设故障。
- `KEYBOARD REPORT RECEIVED` 与 `KEYBOARD KEY DETECTED` 分开，避免空闲报告造成误解。

复测顺序：接 PC 查看 `fastboot devices` / `fastboot getvar product`；改接 OTG 键盘，
按 Caps/Num/Enter 查看字段；保留转接头更换 U 盘；最后接回 PC 再次 getvar。
如果继续卡在等待设备，请反馈 `D/O/P/E/T`；若锁定灯不变，请反馈 `R/K/L/T`。

## UEFI Host 0.1 测试基线（2026-10-18）

本轮首测镜像（位于 Git 根目录）：`Mu-lancelot-usb-host-v0.1-status.img`，604160 bytes。
配套原始 FD：`Mu-lancelot-usb-host-v0.1-status.fd`，2097152 bytes。

```text
7fc41386dd1ab9a1d7d5570173fe37c3b624271a9e6cddd88c7c6a62d2dace61  Mu-lancelot-usb-host-v0.1-status.img
9ea8c9779db9af4a288db84e3bd70c18a245cdb89da594f5468b5382d960f175  Mu-lancelot-usb-host-v0.1-status.fd
```

已实现首版 `MediaTekPkg/Drivers/MusbHostDxe`：MUSB PIO Host、I2C7/WUSB3801/SMB1351
板级支持、UEFI USB2 HC 协议、control/bulk 和键盘所需的 interrupt 传输。
lancelot 固件装入 UsbBus/UsbKb/UsbMassStorage，并增加 USB 键盘 ConIn 路径。
`LANCELOT_USB_HOST=1` 为 0.1 的默认；在 lancelot DSC 将其设为 `0` 可构建设备模式。
0.1 的 Host 与 Device 在固件卷中互斥；该旧 Host 测试版接 PC 时不提供 fastboot。

已完成构建、实际驱动 C 的 ASan/UBSan 回归、FV/镜像载荷检查。**UEFI Host 实机验证待进行**；
下文的 U 盘识别成果来自 Android，不能视作此 UEFI 驱动已实机通过。
实现范围、地址和源码对应关系见
[驱动说明](../Silicon/MediaTek/MediaTekPkg/Drivers/MusbHostDxe/README.md)。

首测按已有方式启动新 UEFI，使用直连 OTG 键盘或 U 盘。键盘可在 Boot Manager 菜单中
验证方向键/Enter；U 盘可检查是否出现 USB 存储或其中的 EFI 启动项。
请记录是否亮灯/供电、是否检测到设备、键盘输入或存储访问结果，以及插拔后的变化。
若通过高速 Hub 接低速/全速设备，需要尚未实现的 split transaction，首测应避开这种拓扑。

按用户要求，Host 版底部现在显示两行英文状态（独立 1 秒刷新）：
`WAITING FOR OTG`、`CHECKING VBUS`、`POWER ON / WAITING DEVICE`、`RESETTING DEVICE`、
`ENUMERATING DEVICE`、`KEYBOARD/STORAGE/HID/HUB CONFIGURED` 和 VID:PID/地址/速度。
收到键盘中断报告后显示 `KEYBOARD INPUT RECEIVED`；错误显示 I2C/Type-C/VBUS/USB 阶段与状态。
配置完成只表示 USB SET_CONFIGURATION 成功，不等于文件系统或键盘按键已实测可用。
若板级初始化失败，模块仅保留状态显示，不发布 USB Host 控制器协议。

```sh
python3 Resources/Scripts/test_lancelot_usb_host.py
python3 build_uefi.py -d lancelot -r RELEASE
python3 Resources/Scripts/audit_lancelot_usb_host.py
```

## 设备模式与取数基线

- 用户报告 Mu 的 USB 遥测版本 `fastboot devices` 正常；此前 EP0 已到 configured。
  当前 Mu 版本的 bulk 命令双向通信仍需单独确认。
- 当前改动移除 `MusbDiagnosticDxe` 的底部字段和屏幕定时器，保留设备模式状态机及 RAM 日志。
- 去屏幕字段的设备模式镜像作为 Host 适配前的基线保留。
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

## UEFI 适配与后续验证

1. 已从用户提供的源码复核 WUSB3801 -> extcon -> SMB1351 VBUS -> MUSB Host 链路。
2. 已接入板级 I2C、角色检测、VBUS masked write/readback；Host/Device 按构建配置互斥。
3. 已实现根端口、control/bulk/sync interrupt、四路异步 interrupt IN 和 USB2 HC 协议。
4. 已将 `UsbBusDxe` / `UsbKbDxe` / `UsbMassStorageDxe` 加入 Host 固件卷并配置键盘路径。
5. 下一步实机验证板级 I2C/VBUS、枚举、键盘和 U 盘，以及插拔/错误恢复/EBS。
   高速 Hub 下的 split transaction、等时传输和完整 PD/动态 Host-Device 切换仍待适配。

## 设备模式基线检查

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
