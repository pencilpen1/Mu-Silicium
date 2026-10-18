# lancelot 移植其他 UEFI：参数、驱动与迁移清单

整理日期：2026-10-18。适用于将本项目成果迁入其他 EDK2 / Project Mu / 自定义 AArch64 UEFI 平台。
本文是当前参数入口；旧 `lancelot_porting_datasheet.md` 仅作历史记录。
下文路径均相对于本项目根目录。

## 1. 设备与证据基线

| 项目 | 当前基线 |
| --- | --- |
| 设备 | Redmi 9，代号 lancelot，MT6768 平台 |
| 实机配置 | 4 GB RAM + 64 GB eMMC；64 GB 是标称容量，不是分区/LBA 边界 |
| 系统、LK、固件 | `DerpFest-v17-20260925-lancelot-Community-Stable.zip` |
| 内核采集 | ShockwaveKernel-S+ `4.19.325-cip136-st20`；构建所用精确源码 commit 未确认 |
| UEFI 基础 | EDK2 stable202302；完整依赖版本见 `docs/dependencies.md` |
| 实机已验证 | 进入 Shell、图形显示正常、USB v0.2 枚举及一次 fastboot getvar 双向通信 |
| 当前 v0.3 | 共享 RAM 启动日志 + USB 读取；构建/宿主测试通过，实机传输待验证 |

参数证据层级：**运行时采集**表示原 Android 已读到的数据；**UEFI 配置**表示当前代码选择；
**实机验证**表示对应功能已有用户测试反馈。DT 中出现设备不等于本项目已有对应 UEFI 驱动。
不同内存版本、屏幕批次、ROM/LK，以及不同大小的新 UEFI 镜像，均可能改变交接布局。

关键材料：

- `analysis/runtime-2026-10-17/lancelot-runtime.dtb`：首次 Android 运行时 FDT，SHA-256
  `41f9af3363ab92bf3ca82a0943afb5ee00d2765df42f8870f457ea175191c27d`。
- `analysis/usb-2026-10-17-root/runtime.dtb`：后续 root USB 采集 FDT，SHA-256
  `f3b3d062e0acecb1c6ebcf2e3490cf6d5f811ce21be3f6a12e6f21a380f2e30a`。
- `analysis/runtime-2026-10-17/fb-info.txt`：FB 参数；`analysis/usb-2026-10-17-root/`：绑定驱动、角色、内核配置。
- `ws/blobs/dtb`：从 ROM boot 提取的 base DTB，**不是** LK 修正后的 runtime DTB，也不是 DTBO。
- `analysis/usb-2026-10-17/` 是早期失败采集，不作硬件依据。

## 2. 启动封装与入口交接

```text
原厂 preloader / ATF / LK
  → Android boot v2（kernel = gzip(BootShim + UEFI FD)）
  → BootShim 搬运 FD
  → SEC / PrePi（保留 x0 FDT，配置 EL、栈、异常、内存图）
  → DXE → BDS → Shell
```

| boot 参数 | 值 | 说明 |
| --- | --- | --- |
| header_version | `2` | 原 ROM 格式 |
| pagesize | `0x800` / 2048 | 打包工具默认值也需确认 |
| base | `0x40078000` | mkbootimg 基址 |
| kernel_offset | `0x00008000` | kernel_addr=`0x40080000` |
| ramdisk_offset | `0x07c08000` | ramdisk_addr=`0x47c80000` |
| tags_offset | `0x0bc08000` | tags_addr=`0x4bc80000` |
| dtb_offset | `0x0bc08000` | dtb_addr=`0x4bc80000`；这是加载地址偏移，不是文件中 DTB 的位置 |
| second_offset | `0x00e88000` | 当前脚本保留项；没有传入 second payload |
| cmdline | `bootopt=64S3,32N2,64N2 androidboot.serialconsole=0` | 当前封装值 |
| ramdisk / DTB | `ws/blobs/initramfs` / `ws/blobs/dtb` | 构建输入 |

完整可运行命令见 `ws/build.sh`，封装器为 `ws/mkbootimg.py`。移植后替换其 FD 输出路径。
FD 的入口分支、链接基址、FDF BaseAddress、BootShim 复制目标及大小必须一致。
本项目 FD 为 `0x200000` 字节；换成更大固件后需要重新规划整个布局。

入口实现：`ws/BootShim/BootShim.S`、`ws/MT6768Pkg/PrePi/AArch64/ModuleEntryPoint.S`。

- Shim 保留 x0；SEC 暂存到 x20，最终交给 `CEntryPoint(FdtPointer)`。
- Shim 把 Linux Image 的 `text_offset` 字段用作绝对 FD 复制地址；这是本 LK 路径的兼容技巧，
  不能当作通用 ARM64 Linux Image 头。移植到另一个 loader 时重审其入口 ABI。
- 当前复制循环每次 16 字节，复制大小需非零且按 16 字节对齐；复制完成执行 DSB / I-cache invalidate / ISB。
- 按入口 CurrentEL 判断；EL2 时设置 HCR_EL2.RW、SCTLR_EL1、CNTHCTL_EL2、CNTVOFF_EL2、CPTR_EL2，
  使用 `SPSR_EL2=0x3c5` ERET 到 EL1h。设置 SP_EL1、屏蔽 DAIF，然后安装异常向量。
- 不把“LK 实际入口一定为 EL2”写死为事实。更高 EL 必须允许所用 GIC/timer 系统寄存器访问。
- MMU/D-cache 时序沿用已审查实现；早期代码使用 `-mstrict-align -mgeneral-regs-only`。
  FDT 只保证 cell 的 4 字节对齐，64 位值用安全的 BE32/byte 组合读取。

## 3. 内存布局

### 3.1 原 Android 运行时 DDR

`/memory/reg = base 0x40000000, size 0x100000000`，范围 `[0x40000000, 0x140000000)`。
这是 **4 GiB 单 bank**，不是全部都可分配。不能把 ROM 静态 DT 的 `0x3e605000` 当实际总 RAM。

后续 root FDT 的全部固定 `/reserved-memory/*/reg` 如下，范围采用左闭右开：

| 用途 | 起点 | 大小 |
| --- | --- | --- |
| ATF reserved | `0x4ce00000` | `0x60000` |
| aee_debug_kinfo | `0x4d000000` | `0x10000` |
| pstore | `0x4d010000` | `0xe0000` |
| minirdump | `0x4d0f0000` | `0x10000` |
| AP/MD non-cache shared | `0x6c000000` | `0x900000` |
| TEE reserved | `0x70000000` | `0x7000000` |
| SPM reserved | `0x77ff0000` | `0x10000` |
| AP/MD cache shared | `0x7a000000` | `0xc00000` |
| framebuffer | `0x7cdb0000` | `0x2250000` |
| TEE/REE shared reserved | `0x7f000000` | `0xc00000` |
| TEE secure memory | `0x7fc00000` | `0x200000` |
| SSPM reserved | `0x7ff00000` | `0xc0000` |
| log_store | `0x7ffc0000` | `0x40000` |
| SCP reserved | `0x9f900000` | `0x600000` |
| CCCI tag | `0x9fff0000` | `0x10000` |
| modem memory | `0xae000000` | `0x10000000` |
| TEE log | `0xbfc00000` | `0x20000` |
| ATF log | `0xbfe00000` | `0x40000` |
| ATF ramdump | `0xbff80000` | `0x80000` |
| DRAMC rank0 | `0x13ffff000` | `0x1000` |

还必须读取 FDT header 指向的 **reservation map**，以及动态预留节点，不能只抄本表。
`alloc-ranges` 是“起点 + 长度”的候选分配窗口，不是最终保留地址；打包资料中的
`hardware.json` 同时导出 reservation map、固定 reg 和动态节点原始属性。

### 3.2 当前 UEFI 自选布局（不是 SoC 固定参数）

| 区域 | 起点 | 大小 | 结束地址 |
| --- | --- | --- | --- |
| FD | `0x4d100000` | `0x00200000` | `0x4d300000` |
| PrePi pool | `0x4d300000` | `0x03fc0000` | `0x512c0000` |
| v0.3 BootLog | `0x512c0000` | `0x00030000` | `0x512f0000` |
| SEC 异常紧急栈 | `0x512f0000` | `0x00010000` | `0x51300000` |
| 主栈 | `0x51300000` | `0x00040000` | `0x51340000` |

`PcdCpuVectorBaseAddress` 在本实现中给异常紧急栈预留范围使用；真正 VBAR 指向
FD 内的 `SecVectorTable`，需按重定位后的地址 2 KiB 对齐，不能直接把上表地址写作 VBAR。

同步修改位置：

- `ws/build_bootshim.sh`、`ws/MT6768Pkg/MT6768Pkg.fdf`：FD 基址/大小。
- `ws/MT6768Pkg/Devices/lancelot.dsc`：pool、stack、显示 PCD。
- `ws/MT6768Pkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c`：固定区域与 FDT 构图。
- `ws/MT6768Pkg/Include/Configuration/BootLogConfig.h`：日志地址。
- 目标 UEFI 自身的 FDF、PCD、链接脚本、异常入口及测试。

内存服务必须保持的语义：可用 RAM 只建立 resource HOB；真正已占用内存才建立 allocation HOB；
栈仅记录一次；pool 内真实页分配由 allocator 记录；TEE 等固件保留区不映射为普通可缓存 RAM；
framebuffer 单独保留并映射。BootLog 为保留内存类型并有 CPU 映射。
当前解析器支持实测的 2/2 cells、单 bank，拒绝不支持的多 bank，而不是假装全部支持。
固定区在解析 FDT 前已被 Shim/栈使用，后面的冲突检查不能事后保护首次写入。

## 4. CPU / GIC / Timer / watchdog / UART

| 项目 | 参数 | 证据/迁移注意 |
| --- | --- | --- |
| CPU | 6×Cortex-A55 + 2×Cortex-A75 | runtime DT compatible；当前 UEFI 只启用 boot CPU |
| CPU reg / MPIDR affinity | `0x000,0x100,0x200,0x300,0x400,0x500,0x600,0x700` | 以 DT reg 为准，不从 cpu 节点名猜；后两项 A75 |
| CPU DT clock-frequency | A55 `1701000000`，A75 `2171000000` Hz | DT 描述，不代表启动瞬间频率 |
| PSCI | `arm,psci-1.0`，`method=smc` | ResetSystem 使用 ArmSmcPsciResetSystemLib；关机/复位/多核功能需单独验证 |
| GIC | `arm,gic-v3` | `PcdArmGicV3WithV2Legacy=FALSE` |
| GICD | `0x0c000000`，size `0x40000` | `PcdGicDistributorBase` |
| GICR | `0x0c040000`，size `0x200000` | `PcdGicRedistributorsBase`；不是 GICC |
| GIC 第三 reg | `0x0c53a650`，size `0x50` | 保留原始 DT 描述；不当成 GICC/ITS 推断使用 |
| GIC maintenance | PPI 9 → INTID 25 | DT flags `4` |
| ARM timer | DT frequency `13000000` Hz | 实际代码读取架构计数器；移植时核对 CNTFRQ_EL0 |
| Timer secure / NS physical / virtual / hyp | INTID **29 / 30 / 27 / 26** | DT PPI **13 / 14 / 11 / 10** 加 16；flags `8` |
| UEFI timer period | `10000` × 100 ns = 1 ms | 给事件/USB 轮询使用 |
| AP watchdog | TOPRGU `0x10007000`，size `0x1000` | MODE key=`0x22000000`，清 bit0 停用；见 PrePi.c |
| UART0 | `0x11002000`，size `0x1000`，SPI112 → INTID144 | `mediatek,mt6577-uart`，LSR=`base+0x14`、TX ready bit=`0x20` |
| UART1 | `0x11003000`，size `0x1000`，SPI113 → INTID145 | 同 compatible |
| UART baud 线索 | `921600` | Android bootargs；无物理串口验证，当前活跃日志不用 UART |

`PcdGicInterruptInterfaceBase` 当前虽也配置 `0x0c040000`，不证明 MT6768 在此有 GICv2 CPU interface。
迁入目标框架时选择正确 GICv3 驱动/系统寄存器访问路径。不要移植无限 UART 轮询。
停用 AP WDT 不代表 PMIC watchdog、电源管理或所有复位源已经适配。

## 5. 显示 / GOP

| 参数 | 当前值 |
| --- | --- |
| 运行时面板名称 | `nt36672A_fhdp_dsi_vdo_dijing_j19_lcm_drv` |
| framebuffer | `0x7cdb0000`，reservation `0x2250000`，末尾 `0x7f000000` |
| 可见尺寸 | `1080 × 2340` |
| 每行像素 / 字节 | **1088 / 4352** |
| bpp / bytes per pixel | **32 / 4** |
| Android 虚拟尺寸 | `1088 × 7104`，不是 UEFI 可见分辨率 |
| GOP 单 surface 大小 | `1088 × 2340 × 4 = 0x9b6400` |
| 当前 GOP PixelFormat | `PixelRedGreenBlueReserved8BitPerColor`；需在新目标验证颜色通道 |
| 写入像素最高字节 | `0xff`，初始化用不透明黑 `0xff000000` |

复用组：`Drivers/SimpleFbDxe` + `Library/FrameBufferBltLib` + 相应显示 PCD + cache maintenance。
目前依赖 **LK 已初始化并维持显示扫描输出**，没有完整 MTK DSI/显示控制器上电驱动。
不透明像素版本实机显示正常，但并未读取硬件混合寄存器来独立证明 alpha 根因。

需要保留：独立 stride、目标矩形边界校验、重叠滚屏方向、写完后脏行 cache flush、适当 TPL。
GOP 接入目标 BDS/ConOut：通过 GraphicsConsole + ConSplitter；本平台 mode 0 为 80×25。
不能同时把 framebuffer SerialPortLib 包装为串口 TerminalDxe 再加入 ConOut，
否则 ANSI 转义序列可能直接画到屏幕。参考 `Library/PlatformBootManagerLib/PlatformBm.c`。

## 6. USB 设备模式：可复用实现及依赖

| 项目 | 参数 |
| --- | --- |
| MAC | MUSB HDRC，`mediatek,mt6768-usb20`，`0x11200000`，size `0x10000` |
| PHY 总窗口 | `0x11cc0000`；MAC 节点第二 reg size=`0x10000` |
| PHY provider / port | generic-tphy-v1：`0x11cc0000` size `0x800`；USB2 port `0x11cc0800` size `0x100` |
| IRQ | SPI97 → GIC INTID **129**，flags=`8`；当前实现轮询而不是使用该 IRQ |
| clocks | `usb0`、`usb0_clk_top_sel`、`usb0_clk_univpll3_d4`，PHY `ref` |
| role | DT=`otg`；当前 UEFI 强制 device/peripheral，PC 供 VBUS |
| 实际 Android 驱动 | `mt_usb`、`musb-hdrc`、`mtk-tphy`、`mtk-extcon-usb` |
| 外部充电/Type-C | I2C `7-0055` = `smb1351-charger`；`7-0060` = `usb_type_c` |
| 供电依赖 | LK 留下的 UNIVPLL、VUSB/VIO18/VA12；无独立 PMIC/Type-C 供电策略 |
| 当前 USB 速度 | full-speed 12 Mbit/s；Android high-speed 能工作不等于 UEFI high-speed 已实现 |
| 接口/端点 | interface 0，EP0=64 bytes，bulk EP1 OUT `0x01` / IN `0x81`，64 bytes |
| VID:PID / serial | `18d1:d00d` / `lancelot-uefi`（诊断标识，不是硬件序列号） |
| 调度 | 1 ms PIO 轮询，TPL_NOTIFY；屏幕回调 TPL_CALLBACK；ExitBootServices detach |

源码：`ws/MT6768Pkg/Drivers/MusbDiagnosticDxe/`，平台在 DSC/FDF 中各注册一次。
v0.3 DEPEX：CPU + Timer Architectural Protocol；还依赖 BootLogLib、BootScreen、font5x12 和
BaseLib / BaseMemoryLib / IoLib / TimerLib / CacheMaintenanceLib / UefiBootServicesTableLib。
只搬一个 `.c` 文件不够。目标框架可替换屏幕与日志后端，但保留状态机和 MMIO 时序。

`HardwareInit()` 的寄存器链：

1. 检查 `0x1000c238 bit0` 的 UNIVPLL；不主动重配共享 PLL。
2. USB mux：`0x10000098` 清选择/PDN，`0x10000094` 设置选择，`0x10000004 bit22` 提交。
3. INFRA gate：`0x10001084 bit8` 清 gate，读 `0x10001090` 与 `0x10000090` 验证。
4. USB2 PHY 在 `0x11cc0800` 上按源码恢复 USB/device 模式、保留校准字段，延时。
5. 禁用旧 DMA/QMU、配置 indexed endpoint/FIFO、清 W1C 状态，开启 D+ soft connect。

详细 PHY 位域序列以 `HardwareInit()` 为准，不用简化表替代 read-modify-write 和时序。
EP0 关键修复：先 ACK SETUP → 等 `RXPKTRDY` 清零读回 → 写 FIFO → TX；
状态完成必须检查 DATAEND；SET_ADDRESS 等状态握手完成后生效；COUNT0 读 8 位。
不要误把 PHY 节点名的 `11210000` 当真实 PHY 地址，真实值看 `reg`。

当前驱动提供诊断 fastboot 查询和 v0.3 `oem log`，**没有通用 USB device-controller protocol**。
接入目标框架的 USB Function/Device 协议时，需要额外适配 endpoint/control/transfer/cancel/reset 接口。
OTG host 还需要 host-controller protocol、interrupt/bulk/control 调度、Type-C/VBUS 供电及 USB 键盘驱动。

## 7. 存储、按键、电源：参数已知但驱动待做

| 模块 | 运行时 DT 参数 | 移植工作 |
| --- | --- | --- |
| eMMC/MSDC0 | `0x11230000/0x10000`，TOP=`0x11cd0000/0x1000`，SPI100→INTID132，8-bit，non-removable | MTK host 初始化、clock/pinctrl/供电、命令/数据传输，再接 BlockIo/DiskIo |
| microSD/MSDC1 | `0x11240000/0x10000`，TOP=`0x11c90000/0x1000`，SPI101→INTID133，4-bit | 卡检测、供电/电压切换、host 协议；DT cd-gpios 指向 GPIO18 |
| MSDC 时钟 | eMMC：source/hclk/source_cg/crypto_clk；SD：source/hclk/source_cg | phandle/clock ID 见 hardware.json 和 DT，不能把 DT cell 直接写成寄存器值 |
| 存储速度 | 两节点 DT max-frequency=200 MHz；eMMC 声明 HS200/HS400 | DT 能力不是已验证 UEFI 工作模式；先实现低速读卡与基本数据传输 |
| keypad | `0x10010000/0x1000`，SPI73→INTID105，`mediatek,kp`，clock=`kpd` | MTK keypad + PMIC 按键输入，到 SimpleTextIn / SimpleTextInEx |
| GPIO | `0x10005000/0x1000` | pinctrl 另有多个 bank；不要以这个单窗口替代完整 pinctrl |
| PMIC wrapper | `0x1000d000/0x1000`，`mediatek,mt6768-pwrap` | wrapper、MT6358 PMIC/regulator/按键/RTC |
| PMIC | DT=`mediatek,mt6358-pmic` | 中断可能经过下级 interrupt-controller，不套用 SPI+32 |
| 时钟 | TOPCKGEN=`0x10000000`，INFRACFG_AO=`0x10001000`，APMIXED=`0x1000c000` | 控制器驱动需要具体 clock mux/gate/reset 流程 |

原 DT 同时有 `mmc@...` 和 `msdc@...` 别名节点；不能重复注册同一硬件。
DT 的 eMMC HS400、Type-C 芯片候选等需结合实际绑定驱动验证。
PMIC 的中断不是 GIC 三 cell；只有 GIC SPI 用 +32、PPI 用 +16。

## 8. 驱动搬运清单

以下目录均在 `ws/MT6768Pkg/`，每组应连同 INF、头文件、PCD、GUID 和构建依赖迁移。

| 目标能力 | 可复用源码 | 目标框架需衔接 |
| --- | --- | --- |
| SEC 入口/异常 | `PrePi/`，外加 `ws/BootShim/` | 目标 SEC ABI、FV 加载、异常向量、栈和 cache |
| RAM/MMU/HOB | `Library/MemoryInitPeiLib/`、`Library/PlatformPeiLib/` | 目标 MemoryMap/MMU/HOB 服务；不要启用旧静态 fallback 表 |
| 最小平台信息 | `Library/MT6768PkgLib/` | 目前只声明 boot CPU，`ArmPlatformInitialize()` 无实质外设初始化 |
| GOP | `Drivers/SimpleFbDxe/`、`Library/FrameBufferBltLib/` | GOP、BLT、PCD、LK framebuffer 继承 |
| 图形文字控制台 | `Drivers/GraphicsConsoleDxe/`、`Drivers/ConSplitterDxe/`、`Library/PlatformBootManagerLib/` | 目标通常已有这些组件，合并需要的修改，避免重复安装 |
| 屏幕诊断 | `Include/Library/BootScreen.h`、`Include/Resources/`、`Library/FrameBufferSerialPortLib/` | 固定 FB/stride、cache flush、SerialPortLib 后端 |
| USB device | `Drivers/MusbDiagnosticDxe/` | CPU/Timer protocols、DXE 调度、日志/屏幕依赖 |
| BootLog | `Library/BootLogLib/`、`Include/Library/BootLogLib.h`、`Include/Configuration/BootLogConfig.h`、`Library/SecScreenDebugLib/` | 内存预留、SEC 初始化、DXE 日志写入；WSL 读取脚本见 tools |
| DXE 早期阶段诊断 | `patches/edk2-lancelot-dxe-probes.patch` | 依赖 MT6768Pkg/BootLogLib，按目标 DXE Core 版本手工合并 |
| eMMC/SD、host USB、实体输入 | 尚无完整本机实现 | 上一节参数作为开发起点 |

`Drivers/MT6768PkgDxe/` 的 `InitPeripherals()` 基本为空，不能当成已集成完整 SoC 初始化。
`Drivers/SmbiosPlatformDxe/`、`AcpiTables/` 有参考平台遗留，不能直接视为 lancelot 正确硬件描述：
例如 DSDT 仍含 `ARM-JUNO`，MADT 的 GICR 长度为 `0x100000`，而 runtime DT 为 `0x200000`。
启动 Linux/Windows 需要按目标 OS 重新整理 CPU/MADT、GTDT、DSDT、PSCI、电源、存储和驱动，
“能进 Shell”并不覆盖这部分。

本地 MUSB 实现的 SPDX 为 **GPL-2.0-only**；其他文件保留各自 BSD/上游许可声明。
移入新 UEFI 树时保留文件许可证和来源。Linux `.ko` 不能直接作为 UEFI DXE 驱动加载。

## 9. 参考源码定位

| 来源 | 版本 / 用途 |
| --- | --- |
| https://github.com/xiaomi-blossom-dev/edk2-mt6765 | `debee5b0ff6f504ff2264cee980f95c5293b4fa6`，本平台包祖先；其设备常量不能直接用 |
| https://github.com/Project-Silicium/Mu-Silicium/tree/main/Platforms/Xiaomi/lancelotPkg | lancelot 另一套 UEFI；用于入口/内存配置对照，main 为可变分支，本文未固定其 commit |
| https://github.com/MrShockWAVEog/android_kernel_xiaomi_mt6768-r | `3d974afe57c2431145debf1c788bd2cc7676e0b2`，USB/clock 寄存器序列参考 |
| https://github.com/mt6768-dev/android_kernel_xiaomi_mt6768 | `9aa3fdfd82e94fcf7ed8fc5a8458e44dd9c479a4`，generic TPHY 参考 |

已对照的内核文件：`drivers/misc/mediatek/usb20/{mtk_musb_reg.h,musb_core.c,musb_gadget_ep0.c,mt6768/usb20_phy.c}`、
`drivers/clk/mediatek/clk-mt6768.c`、`drivers/phy/mediatek/phy-mtk-tphy.c`。
存储/电源继续在所选内核树内定位 `mediatek,mt6768-mmc`、MSDC、MT6358、pwrap、SMB1351、
Type-C 的 compatible/驱动绑定；不要假定不同分支的源码路径一致。

## 10. 推荐迁移顺序与资料包

1. 先接 boot 封装、x0/FDT、入口 EL/栈/向量，确认 SEC 阶段标记。
2. 对照当前设备的 runtime reservations 布局，接 MMU/HOB，进入 DXE。
3. 接 GICv3/架构 timer，确认事件推进；复用 LK framebuffer，进入可见 Shell。
4. 接 MUSB peripheral，先 getvar，再用 BootLog 抓启动日志。
5. 接输入和存储；最后按 OS 目标完善 ACPI/DT、运行时服务、电源与退出交接。

在 WSL/Linux 导出源码与证据：

```sh
python3 tools/export_lancelot_porting.py --output /tmp/opencode/lancelot-porting-kit.tar.gz
tar -xzf /tmp/opencode/lancelot-porting-kit.tar.gz -C /你的资料目录
```

包内保留目录结构，包含平台包、Shim 源码、构建脚本、DTB/initramfs、测试、文档、EDK2 补丁、
两份 runtime DTB 和必要硬件采集。`hardware.json` 自动提取 runtime DT 原始设备属性、保留区、
当前 PCD/日志布局；`MANIFEST.sha256` 校验所有打包文件。`KIT.md` 是解包入口。
该包是移植素材，外部 EDK2 依赖版本见 dependencies.md；完整 ROM 分区、构建树、镜像另在本项目本地保留。

可选复核（配置好对应 EDK2 依赖后）：

```sh
python3 tools/test_lancelot_fdt.py
python3 tools/test_lancelot_gop.py
python3 tools/test_lancelot_musb.py
python3 tools/test_lancelot_bootlog.py
python3 tools/test_lancelot_log_reader.py
```

`tools/audit_lancelot_boot.py` 另需 `rom-extract/boot.img` 及构建产物，用于新镜像封装/入口/区域审计。
日志工具及 WSL USB 透传要求见 `docs/lancelot_usb_logging.md`。
