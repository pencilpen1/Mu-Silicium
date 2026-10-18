# Redmi 9 (lancelot) / MT6768 — EDK2 UEFI 移植数据表

> 本文主要记录最初取数，不能当作当前参数。移植其他 UEFI 请看
> [参数、驱动与迁移清单](lancelot_uefi_porting_guide.md)。旧 FD `0x70000000`
> 已确认与 TEE 重叠，当前使用 `0x4d100000`；旧 framebuffer/panel 值也已被运行时采集纠正。

源 ROM: `DerpFest-v17-20260925-lancelot-Community-Stable.zip`
参考树: `refs/edk2-mt6765`（xiaomi-blossom-dev，基于 edk2-exynos7885）
目标包名: `MT6768Pkg`（fork 自 MT6765Pkg）

用户已确认：实机为 **4 GB RAM + 64 GB 存储**；当前系统、LK 和固件均来自上述 ROM。
ROM 静态 DT 的内存大小不是整机容量；仍需确认 LK 修正后的 bank、保留区和 framebuffer 地址。

## 启动链
DerpFest boot.img = Android boot header **v2**，page_size 0x800。
- kernel_addr  = 0x40080000  → mkbootimg base=0x40078000, kernel_offset=0x8000
- ramdisk_addr = 0x47c80000  → ramdisk_offset = 0x7c08000
- tags_addr    = 0x4bc80000  → tags_offset    = 0xbc08000
- second_addr  = 0x0
- cmdline      = `bootopt=64S3,32N2,64N2 androidboot.serialconsole=0`
- (dtb 在 boot.img 末尾 0xe9c000，但 mt6765 参考是单独 --dtb blobs/dtb；lancelot 需要决定是否复用 boot.img 内嵌 DTB 还是单独 dtbo)

## 关键地址（→ 填到 MT6765Pkg 对应文件）

| 项 | lancelot 值 | mt6765 参考值 | 填到 |
|---|---|---|---|
| DDR base | 0x40000000 | 0x40000000 | `Devices/<codename>.dsc` PcdSystemMemoryBase |
| DDR size（DT 静态） | 0x3e605000（≈1GB，**可能被 LK 运行时改写**） | 0xc0000000 | PcdSystemMemorySize（需运行时确认） |
| GIC distributor (GICD) | 0x0c000000 | 0x0c000000 | `MT6765Pkg.dsc` PcdGicDistributorBase |
| GIC redistributor (GICR) | 0x0c040000（size 0x200000） | 0x0c100000 | PcdGicInterruptInterfaceBase（注意：GICv3 redistributor ≠ CPU iface，需核对 ArmGicDxe） |
| UART0 (debug) | 0x11002000（THR），LSR=0x11002014（bit 0x20） | 同 | `UartSerialPortLib.c` + `PrePi.c` 硬编码 |
| UART1 | 0x11003000 | — | 备用 |
| UART 波特 | 921600（chosen bootargs `ttyS0,921600n1`） | — | — |
| GPIO | 0x10005000 | — | — |
| pinctrl | mediatek,mt6768-pinctrl @ gpio@10005000 | mt6765-pinctrl | — |
| keypad (kp) | 0x10010000（mediatek,kp） | — | `BootDevices.h` 按键 device path |
| sys_cirq | 0x10204000 | — | — |
| SSPM | 0x10440000（reg） | — | — |
| SCP | 0x10500000（reg 0x80000） | — | — |
| topckgen | 0x10000000 | — | — |
| infracfg_ao | 0x10001000 | — | — |
| pericfg | 0x10003000 | — | — |
| chipid | 0x08000000 | — | — |
| mm_mutex | 0x14016000 | — | — |
| **FrameBuffer base** | **0x7e605000**（atag,videolfb-fb_base_l） | 0x7C2D4800 | PcdMipiFrameBufferAddress |
| FrameBuffer vramSize | 0x17e8000（≈24MB） | 0xE20000 | Display Reserved 区大小 |
| Panel | hx83112b_fhdp_dsi_cmd_auo_rt5081_drv | — | — |
| 分辨率 | **1080 × 2340**（tpd-resolution 0x438×0x924） | 720×1440 | PcdMipiFrameBufferWidth/Height(/Visible) |
| BPP | 32 | 32 | PcdMipiFrameBufferPixelBpp |

## reserved-memory（DT，→ DeviceMemoryMap.h）
**更正：alloc-ranges tuple 是起点 + 长度，不是起点 + 上限；窗口不代表实际分配地址。**
- ssmr-reserved-cma_memory: size 0x10000000(256M), 原属性名 `alloc-range`，窗口 [0xc0000000, 0xd0000000)
- reserve-memory-sspm_share: size 0x510000, alloc-ranges [0x40000000, 0xa0000000)
- reserve-memory-scp_share: size 0x300000(boot) / **0xd00000**(dtbo 覆盖), alloc-ranges [0x40000000, 0x90000000)
- ion-carveout-heap: size 0x9000, alloc-ranges [0xc0000000, 0x4c0000000)
- consys-reserve-memory: size 0x400000, alloc-ranges [0x40000000, 0xc0000000)
- wifi-reserve-memory: boot DT size 0x300000, alloc-ranges [0x40000000, 0xc0000000)

**注意**：ATF/TEE 等固件保留区需要从 preloader/LK 运行时信息确认；部分
reserved-memory 动态节点也可能由 Linux 分配，不能假定 LK 一定补齐所有 `reg`。
静态 DT 不能据这些窗口确定固件实际地址（preloader 串里有 `lca_reserved_mem start/size`）。
mt6765 参考的 DeviceMemoryMap.h 里那些固定 ATF/TEE/SCP 地址是从其 stock DT 抠的，lancelot 不能照搬。

## 最初 UEFI 布局（历史记录，当前已不使用；现布局见启动审计）
- UEFI FD:        0x50000000, size 0x200000  ← ⚠ 位于 SSPM 候选窗口内，实际是否冲突需运行时验证
- UEFI Stack:      0x40C00000, 0x40000
- CPU Vectors:     0x40C40000
- UEFI MemPool:    0x40D00000, 0x03300000
- BootShim UEFI_BASE=0x50000000, UEFI_SIZE=0x200000

## 初期工具链记录（已过时，当前工具链可完整构建）
- aarch64-linux-gnu-gcc / objcopy: ❌
- mkbootimg: ❌
- iasl (ACPICA, ACPI 表编译): ❌
- dtc: ❌
- edk2 @ edk2-stable202302 (recursive): ⚠ 现为 master c141577，需切 tag + submodule
- edk2-platforms: ❌ 未拉
- sudo: 需密码（装包要用户配合 `!` 前缀）

## 镜像产物
`rom-extract/`: boot.img, dtbo.img, lk.img, preloader_emmc.img, tee/sspm/scp/spmfw.img, vbmeta, logo, dynamic_partitions_op_list, metadata
`analysis/`: boot_dtb.txt, dtbo_dtb.txt（完整 DT 树 dump）
`tools/`: extract_rom.py, dt_parse.py

## 待运行时确认（可先从正常 Android 导出 FDT，不要求先取得 UART LOG）
1. 4 GB DDR 的实际 bank/可用范围（不能照抄静态 DT 的 0x3e605000）
2. SSPM/SCP/ATF/TEE 实际放置地址 → 决定当前 UEFI FD 0x70000000 等区域是否安全
3. FrameBuffer 是否真在 0x7e605000（LK 可能改）
4. mt6765 上 SimpleFbDxe 是坏的，lancelot 的 MTK FB 同样可能要靠 FrameBufferSerialLib
