# lancelot UEFI 进度与一屏 LOGO 问题审计

> 本文包含历史布局与已被修正的推断。当前“绿色”停点复核、修复和测试结果见
> [屏幕诊断复核（AUDIT2）](lancelot_screen_audit_2026-10-17.md)。仅凭旧绿色条不能认定卡在 SEC/UartInit。

审计日期：2026-10-17。对象：当前 `ws/MT6768Pkg`、BootShim、实际生成的镜像、ROM 中的 DTB/DTBO/LK，以及 MT6765 参考树。

**用户确认的实机条件：Redmi 9 / lancelot，4 GB RAM + 64 GB 存储；当前系统、LK 与固件均来源于工作目录中的 DerpFest ROM。UEFI 镜像刷入 boot 后停留在原厂 Redmi LOGO，随后重启；刷回原 boot 能够正常启动。** 已通过 ADB root 取得正常 Android 的运行时 FDT，运行时内存/显示范围不再未知。

原 boot 恢复正常是有价值的对照：当前固件组合至少能启动原系统，故障与替换 boot 的内容/兼容性相关。它不能证明 LK 已接受并执行 UEFI，也不能区分 UEFI 内部异常与新 boot 的校验/解压/交接失败。当前没有依据为排除此故障去改刷 LK、preloader、TEE 或 DTBO。ADB 读到的 `ro.boot.bootreason=lk_crash` 不能单独证明是看门狗或 UEFI 异常。

**结论：已确认原 UEFI 地址直接覆盖运行时 TEE reserved，是本次卡 Redmi LOGO 后重启的首要确定性根因。上一版修复关闭 watchdog 后出现“卡住不重启”，进一步确认 UART 轮询也会造成无声死锁：UART `LSR bit 0x20` 未就绪时，第一条 DEBUG 无限等待。最新修复版已改为 framebuffer 输出 + 非阻塞 UART 镜像并重新编译。实机启动结果仍需刷入后验证。**

修复已改动启动源码并重新生成 `boot-uefi.img`；没有执行刷写操作。只读审计命令仍可用于复核：

```sh
python3 tools/audit_lancelot_boot.py
```

## 1. 实际进度

| 项目 | 当前状态 | 证据/限制 |
| --- | --- | --- |
| ROM 拆包、DT/DTBO 分析 | 已完成基础工作 | `rom-extract/`、`analysis/` |
| 工具链 | 已具备 | GCC/objdump、iasl、dtc 均在本机；旧数据表已过时 |
| EDK2 | 已切到正确基础版本 | `ws/edk2` = `f80f052277`，`edk2-stable202302`；未发现受跟踪文件修改 |
| edk2-platforms | 已拉取 | `1c500d13a6`；未发现受跟踪文件修改 |
| 平台包 | 已从 MT6765 fork 并改名 | `ws/MT6768Pkg`、`Devices/lancelot.dsc` |
| 编译、FD 和 Android 封装 | 已成功 | `ws/16:639-657` 是**主机构建日志，不是手机启动日志** |
| BootShim → FD | 静态布局一致 | 复制目标 `0x70000000`、大小 2 MiB；镜像载荷确为当前 Shim + FD |
| x0/FDT 传递 | 已写入代码，未实机确认 | Shim 保留 x0；SEC 用 x20 暂存后传给 `CEntryPoint()` |
| 动态内存图 | 已实现初版，已确认会覆盖 TEE 区 | 运行时 UEFI 区与 `[0x70000000,0x77000000)` TEE reservation 重叠；非对齐读取、FB 映射、重叠拒绝和 fallback 仍需修正 |
| GIC / Timer | 地址已部分适配，Timer 配置错误 | GICD/GICR 与 DT 一致；物理、虚拟 Timer INTID 不一致 |
| 屏幕日志、GOP | 已确认参数错误 | 运行时 FB=`0x7cdb0000`、size=`0x2250000`、stride=`4352`、panel=`nt36672A...dsi_vdo`；代码仍写死另一套地址/步幅/panel |
| eMMC、USB 控制器、实体按键 | 尚无完整设备适配 | USB Bus/MassStorage 等通用驱动不等于 MTK 控制器驱动；未见 MSDC/eMMC、对应 USB 控制器、实体按键实现 |
| Shell / 操作系统 | 仅已打包 Shell 等组件 | MT6765 README 的“Boots to UEFI Shell”不能当成 lancelot 验证结果；更不能视为已能启动 Android/Windows |

当前有效布局，不再是旧数据表里的 `0x50000000`：

```text
实际 SEC 栈    [0x6ffc0000, 0x70000000)
UEFI FD        [0x70000000, 0x70200000)
UEFI 内存池    [0x70200000, 0x74200000)
CPU vectors    [0x74200000, 0x74210000)
写死的 FB      [0x7e605000, 0x7fe05000)  （预留长度向上取整为 0x1800000）
```

以上是当前代码的写死布局，**不是实机可用布局**。ADB root 取得的运行时关键范围为：

```text
DDR            [0x40000000, 0x140000000)       # 4 GiB，单个 memory/reg tuple
TEE reserved   [0x70000000, 0x77000000)        # mblock-8-tee-reserved
Framebuffer    [0x7cdb0000, 0x7f000000)        # mblock-14-framebuffer
SSPM reserved  [0x7ff00000, 0x7ffc0000)
SCP reserved   [0x9f900000, 0x9ff00000)
ATF reserved   [0x4ce00000, 0x4ce60000)
```

运行时 FDT：`analysis/runtime-2026-10-17/lancelot-runtime.dtb`，SHA-256：
`41f9af3363ab92bf3ca82a0943afb5ee00d2765df42f8870f457ea175191c27d`。

## 2. 先排除什么

当前镜像 `ws/boot-uefi.img`：878592 字节；SHA-256：

```text
2ebc88cd18862d9c7023963c3894dc26ebd5aca807385e606fdcc183948a8187
```

离线检查确认：

- Android boot header v2，page size `0x800`。
- kernel/ramdisk/tags/DTB 地址分别为 `0x40080000` / `0x47c80000` / `0x4bc80000` / `0x4bc80000`，与所提取 ROM 一致。
- 打包 DTB 与 ROM 的 boot DTB **逐字节一致**；不是把 DTBO 当作 base DTB 打包。
- gzip 可正常解压；解压后 `0x200070` 字节 = Shim `0x70` + FD `0x200000`，与当前两个产物逐字节一致。
- FD 首条跳转目标 `0x70008818`，与 `FV/FVMAIN_COMPACT.Fv.map:5` 的 SEC 入口一致。
- `ws/16` 显示构建成功；FVMAIN 的 `100%Full` 是内部 FV 按使用量生成，并非外层 2 MiB FD 溢出；外层仅使用约 36%。
- ADB root 确认实机 `MemTotal=3773012 kB`，运行时 `/memory/reg` 是单段 4 GiB；当前设备不是“只识别到 1 GiB”的情况。

因此，**未发现“FD 和 Shim 地址没一起改”“gzip 损坏”“载荷拼接不对”“编译失败但误拿残留镜像”这类直接证据**。这些检查不证明 LK 已通过校验、已解压或已跳入 Shim，也不证明用户刷入的正是这个哈希的文件。

## 3. 按优先级排列的问题

### P0（ADB root 已直接证实）：UEFI 搬运区与 TEE reserved 精确重叠

这已经不是“可能冲突”。运行时 FDT 的固定节点为：

```text
/reserved-memory/mblock-8-tee-reserved/reg = [0x70000000, 0x77000000)
```

而当前代码使用：

```text
BootShim copy destination / UEFI FD = [0x70000000, 0x70200000)
UEFI MemPool                       = [0x70200000, 0x74200000)
CPU vectors                       = [0x74200000, 0x74210000)
```

`BootShim.S:43-47` 在 UEFI 解析 x0/FDT **之前**就把完整 FD 写入 `0x70000000`；因此后面的动态内存图即使解析正确，也无法挽救首次写入。TEE 区是固定 `no-map` 保留区，不能作为普通 Non-secure UEFI RAM。该冲突可导致安全属性/总线异常或 TEE 内存破坏，随后复位；与“原厂 LOGO 后重启、原 boot 正常”相符。

更严重的是，`MemoryInitPeiLib.c:437-444` 对 UEFI carveout 与 runtime reservation 采用静默裁剪而不是拒绝重叠；这不能使物理上的首次越界写入变安全。

**当前首要根因已锁定为：UEFI FD/栈/pool/vectors 的固定地址选错，直接占用 TEE 保留区。** 必须先重新选择运行时确认的非 reserved 区，并让 BootShim、FD PCD、栈、pool、vectors 和 MMU/HOB 使用同一布局；不能只改 `PcdSystemMemorySize`。

从当前 Android `iomem` 看，`[0x4d100000,0x6fffffff)` 是未显示为固定保留的一段 System RAM，可作为候选搬运区，但还需核对 LK 在 UEFI boot 路径下的 mblock 分配，不能未经检查直接刷写。

### P1：ADB root 已确认 framebuffer 参数也是另一套硬件/运行时值

运行时证据：

- dmesg：`disphal_allocate_fb, pa_start=0x7cdb0000, pa_end=0x7effffff`，即 reservation `[0x7cdb0000,0x7f000000)`。
- `/sys/class/graphics/fb0/virtual_size`：`1088,7104`；`bits_per_pixel=32`；`stride=4352`。
- 运行时 packed `chosen/atag,videolfb` 与 cmdline 指向 `nt36672A_fhdp_dsi_vdo_dijing_j19_lcm_drv`。

当前代码仍使用 `0x7e605000`、`0x1800000`、`1080x2340` 和 `hx83112b...dsi_cmd`。运行时 FDT 里旧的 `atag,videolfb-fb_base_l=0x7e605000` 等 legacy 属性仍残留，不能优先使用；应以 runtime framebuffer reservation/packed property 为准。FrameBufferSerialLib 还按 `1080*4=4320` 使用行步幅，而实机是 4352，所以即便搬运区修好，FB 日志也可能不可见或写坏邻近区域。

### P0：MMU 前 FDT 的 64 位非对齐读取——最值得先修的实际故障候选

代码链：

1. `PrePi/AArch64/ModuleEntryPoint.S:56-63` 关闭 MMU。
2. `PrePi.c:103-104` 调 `MemoryPeim()`，由它解析 FDT 后再配置 MMU。
3. `Library/MemoryInitPeiLib/MemoryInitPeiLib.c:87-94` 的 `Fdt64()` 使用 `*(CONST UINT64 *)p`。
4. FDT property value 只保证 **4 字节对齐**，不保证 8 字节对齐。

当前 `ws/blobs/dtb` 的 `/memory/reg` 数据偏移是 **`0x16e4`，模 8 = 4**。即使 FDT 基址按 8 字节对齐，两次 `Fdt64(data)` / `Fdt64(data + 8)` 仍然都未对齐。

这不是只看源码猜测：

- 实际 `PrePi.debug` 中 `Fdt64+0x8`（ELF 地址 `0x1b40`）生成的是 `ldr x1, [x0]`，不是逐字节安全读取。
- 提取原函数，用 GCC `-fsanitize=alignment -fno-sanitize-recover=alignment` 读取上述 DTB，宿主检查在原文件 **93 行**报告 `load of misaligned address ... for type 'const UINT64'`。

在通常的 AArch64 MMU-off、Device 数据访问属性下，非对齐 64 位读取可触发 Data Abort；仅打开 D-cache 不等于将内存变成 Normal memory。实际行为还依赖入口 EL 和上级控制寄存器，不能用宿主 UBSan 代替真机异常记录。

**建议：将 FDT 64 位读取改为两个对齐安全的 BE32 cell 组合，或明确逐字节读取；不能继续把 FDT 数据强转 `UINT64 *`。** 对完全不可信的 FDT 基址，BE32 也应使用安全的非对齐读取。应一并检查总长度、offset、属性长度、节点名长度/深度、多 bank `reg` 和 reservation map 边界。

证据边界：LK 会修改/扩展 FDT，运行时偏移未必仍为 `0x16e4`；若 x0 无有效 FDT，则走 fallback，不触发此条。这里确认的是**现有实现有具体且可复现的错误，足以在正常入口条件下阻断启动**，不是已经读到了实机 ESR。

### P1：动态内存图漏掉原始 DT 边界外的 framebuffer——修好读取后仍可能马上异常

原始磁盘 DT 的 `/memory/reg` 是 base `0x40000000`、size `0x3e605000`，因此 **原始 DT 下 DDR/HLOS RAM 的末尾恰好是旧 FB 起点 `0x7e605000`**。ADB root 已确认运行时 DDR 实际为 4 GiB、FB 已迁到 `0x7cdb0000`；这进一步证明不能使用磁盘 DT/legacy FB 值作为运行时布局。

`MemoryInitPeiLib.c:415` 将 Display 放进 carveout 列表，然后 `:431-435` 用下面的条件遍历：

```c
if (a >= DdrEnd) {
  break;
}
```

对原始磁盘 DT 数据，Display 的 `a == DdrEnd`，因此**在添加 Display 映射之前退出**。运行时 FDT 下虽然 FB 位于 DDR 内部，但代码仍使用错误的旧 FB carveout；实际 framebuffer 不能由当前 PCD 得到正确映射。

在这种布局下，`ArmConfigureMmu()` 成功开启 MMU 后，`ApplyMap()` 的下一条 `DEBUG("Configure MMU Out")` 就通过 FB 日志库写未映射地址，可触发 translation fault。随后又撞上错误的早期 VBAR，成为无声异常。它与前一条非对齐问题是**独立故障**，不能只修 FDT 读取就认为启动链已打通。

**建议：把 Display/MMIO 等非 Conventional RAM 的必要映射独立加入，不要只在可用 DDR 区间内裁剪发布；还应正确处理真实 runtime FB 地址。** 实机运行时的 RAM 末尾和 FB 地址仍需确认；若它们发生改变，不应直接套用磁盘 DT 的数值推断实际停点。

### P1：早期异常向量安装错误——让上述异常变成无声卡死

`PrePi/AArch64/ModuleEntryPoint.S:65-88`：

```asm
ldr x0, =0x74200000
...                       // 填写向量区域
mrs x0, CurrentEl         // 覆盖了向量地址
...
msr vbar_el1, x0          // 写入的是 CurrentEL，不是 0x74200000
```

- EL1 下 `CurrentEL` 是 `4`，EL2 下是 `8`，均不可能是合法向量表基址；低位还属于对齐/保留位。
- `cmp x0, #0x5` 也不是 EL2 判断（EL2 是 `0x8`）。
- 所有分支最终都落入写 `VBAR_EL1`，EL2 的向量没有相应安装。
- 当前处理方式即使装对，也只是原地死循环，没有异常类型/ESR/FAR 信息。

此错误继承自参考包，**不是每次执行到这里就必然异常**；它解释的是为什么后续哪怕一次访问出错也没有诊断。应按实际 EL 安装对齐的有效向量，并在最早期提供不依赖完整图形栈的异常标记。

### P0：`0x70000000` 安全性的依据错误；动态解析无法事后保护首次搬运

`MemoryInitPeiLib.c:39-42` 说高地址“never collide”，但依据不成立。DT `alloc-ranges` 的每个 tuple 是 **地址 + 长度**，不是地址 + 结束地址。

| 节点 | DT cell 含义 | 正确窗口 |
| --- | --- | --- |
| SSPM | base `0x40000000`，size `0x60000000` | `[0x40000000, 0xa0000000)` |
| SCP | base `0x40000000`，size `0x50000000` | `[0x40000000, 0x90000000)` |
| CONSYS / Wi-Fi | base `0x40000000`，size `0x80000000` | `[0x40000000, 0xc0000000)` |

UEFI 的 FD/pool/vectors 现在不只是落入候选窗口，而是直接落在运行时 fixed TEE reservation `[0x70000000,0x77000000)` 中；这是实际冲突。SSPM/SCP 的 alloc-ranges 仍不能当固定地址，但本次主因已有 TEE `reg` 证据。

更根本的时序问题：`BootShim.S:43-47` 在任何 FDT 检查之前就把 2 MiB 写入目标；SEC 随后直接使用固定栈/向量，`HobConstructor()` 还会在解析 FDT 前写固定内存池。即使后面的动态解析正确，也来不及发现并阻止这些首次写入。

此外，动态构图将 UEFI 区和 reservation 一起排序；`MemoryInitPeiLib.c:437-444` 采用“先出现者保留，后者裁剪”的方式处理重叠，没有把 UEFI 与固件保留区冲突当成错误。地址相同或部分重叠时仍可能发布被占用的内存。

**建议：在首次搬运/用栈前确定可用区并拒绝冲突，或用已确认的运行时内存布局固定地址；不得再凭 alloc-ranges 推断安全地址。** 本机当前至少必须避开所有运行时 fixed `reserved-memory/reg`，特别是 `[0x70000000,0x77000000)`。

### P1：物理/虚拟 Timer 的配置是错的，会造成 DXE/BDS 无时钟推进

`MT6768Pkg.dsc:119-120`：

```text
PcdArmArchTimerIntrNum     = 0x12 = 18   （错误）
PcdArmArchTimerVirtIntrNum = 0x13 = 19   （错误）
```

`analysis/boot_dtb.txt:406-410` 的 PPI 编号必须 **+16** 转为 GIC INTID：

| Timer | DT PPI | 正确 INTID | 当前 |
| --- | --- | --- | --- |
| secure | 13 | 29 / `0x1d` | 默认 29 |
| non-secure physical | 14 | 30 / `0x1e` | **18** |
| virtual | 11 | 27 / `0x1b` | **19** |
| hypervisor | 10 | 26 / `0x1a` | 默认 26 |

这不是仅影响将来 OS 的 ACPI：当前 `ArmGenericTimerPhyCounterLib` 实际操作 `CNTP_*`，`TimerDxe.c:385-405` 用这些 PCD 注册/启用中断源。物理 Timer 发 INTID 30，却未在 30 注册处理函数，UEFI 的定时事件、等待/超时逻辑可能停止推进。具体表现取决于其他中断状态。

GICD `0x0c000000`、GICR `0x0c040000` 与 base DT 一致，原生 GICv3 路径也确实读取 `PcdGicRedistributorsBase`。暂不应把同值的 `PcdGicInterruptInterfaceBase` 直接认定为当前根因。

### P1：所有调试输出依赖未经验证的显示缓冲；“一屏 LOGO”可能只是不可见

- 生效的 `SerialPortLib` 是 `MT6768Pkg.dsc:78` 的 **FrameBufferSerialPortLib**；后面的定义覆盖前面的 UART 库选择，实际 SEC 产物也确认链接到了 FB 实现。
- `PrePi.c` 虽有 UART 寄存器和函数，但 `PrintUart()` 的调用都被注释，不能当成已有 UART 双通道日志。
- FB 地址 `0x7e605000` 来自 **DTBO 的静态属性**，不是读取当前 LK 传入的运行时 framebuffer。早期第一批 DEBUG 就向该地址写字，发生在动态内存图建立前。
- FrameBufferSerialPortLib 和 SimpleFbDxe 都把每行像素数当成 1080；没有独立 pitch/stride，也没有接管 MTK 显示控制器。是否有对齐填充需实测，不能直接假定紧密排列。
- panel 名含 `dsi_cmd`；命令模式下 CPU 改 RAM 并清 cache，并不自动证明面板会收到新的帧。现有代码没有相应刷新触发/控制器适配；LK 是否保持刷新循环未知。
- 参考树 README 本就指出 MTK SimpleFbDxe 存在问题，不能将其视为 lancelot 已有可靠 GOP。

**因此至少存在两条都能解释现象的路径：**

```text
真实早期异常：FDT 访问/漏映射 FB/固定内存写入出错 → 无效 VBAR → 停在 LK LOGO
不可见执行：UEFI 在运行 → 写错 FB/步幅或无 DSI 刷新 → 屏幕仍是 LK LOGO
```

用户已确认看到的是原厂 Redmi LOGO，因此没有证据将停点后移到 DXE/BDS/Shell；但在显示通道未验证前，同样不能据此证明 Shim/SEC 从未执行。

### P1：新增“随后重启”现象——必须区分硬件看门狗与早期崩溃

已确认的信息：

- DT 中 AP/TOPRGU 看门狗寄存器区在 `0x10007000`（`analysis/boot_dtb.txt:751-756`）。
- 当前 BootShim 和平台包未见该硬件看门狗的停用/喂狗实现。
- 打包的 `MdeModulePkg/Universal/WatchdogTimerDxe` 是 **UEFI 事件实现的软件看门狗**：`WatchdogTimer.c:157-161,223-229` 调 `SetTimer()` / `CreateEvent()`，不是接管 MTK TOPRGU。把它加入 FV 不代表硬件看门狗已经处理。
- 对提取的 `rom-extract/lk.img` 做静态反汇编，WDT 初始化函数（VA `0x4c45c8bc`，按该 LK 的 `0x4c400000` 加载基址计算）存在以下路径：

  ```text
  0x4c45c93e: movs r0, #0xa
  0x4c45c948: bl   0x4c45c734       ; 超时设置函数
  0x4c45c954..964: 更新 0x10007000，OR 0x22000000 与 0x5d
                                  ; 其中包含使能位 bit 0
  0x4c45c978: b.w  0x4c45c750       ; 向 0x10007008 写 0x1971
  ```

  超时设置函数将参数左移 11 位、OR `8` 后写入 `0x10007004`，符合该 MTK 看门狗常见的秒数编码，参数 `10` 是约 **10 秒配置的线索**。它不表示从 Redmi LOGO 出现起必定正好剩余 10 秒：计时可能更早开始，也可能被再次配置/喂狗。

这使“LK 留下使能的硬件看门狗，UEFI 早期异常/无进展且无人喂狗，最终复位”成为合理候选，也存在“UEFI 其实在运行但不可见，单纯没有接管看门狗而复位”的可能。

**不能由重启直接判定硬件看门狗是唯一根因。** 用户已确认 LK/固件来源与提取 ROM 一致，但提取 LK 也包含 WDT 停用函数，实际交接路径是否执行、交接后 WDT 状态、是否有 PMIC/其他复位源，都还未知。镜像校验、DTBO 应用或解压在 LK 内失败，同样可能停在原厂 LOGO 后复位；boot 分区刷写成功不等于启动校验和交接成功。

下一轮应记录 LOGO 到复位的时间；在带阶段标记的最小诊断版本中核验交接时 WDT 状态，并按确认过的芯片寄存器语义停用或维护 AP WDT。**只停掉看门狗若将“重启”变为“永远卡 LOGO”，只说明遮住了复位机制，不代表修好了启动。** 不应未经验证地改 PMIC/看门狗复位寄存器，也不需要为此刷写 LK/preloader。

### P2：已确认 4 GB RAM；当前 FDT 解析器仍不支持多 bank

实机容量现在已知，不能再把磁盘 DT 的 `0x3e605000` 当成整机 RAM 容量；它只是静态 DT 中的一段内存描述。4 GB 也不等于 UEFI 可分配的连续 4 GB：还需要扣除固件保留区并尊重实际 bank/洞。64 GB 存储容量不决定早期 DDR 映射。

`MemoryInitPeiLib.c:189-197` 对每个 memory 节点只读首组 `reg`，但不同节点又会覆盖同一对 `DdrBase/DdrSize`。本次已提取**当前原函数**并在宿主编译，以合成 DTB 验证：

| 合成输入（不是实机布局） | 当前函数输出 | 问题 |
| --- | --- | --- |
| 单节点，单 tuple，size = 4 GiB | 正确返回 `0x100000000` | `Fdt64()` 返回类型本身能容纳 4 GiB |
| 单节点，两个 tuple，分别 1 GiB + 3 GiB | 只返回第一段 1 GiB | 后续 bank 被忽略 |
| 两个 memory 节点，分别 1 GiB + 3 GiB | 只返回最后节点的 3 GiB | 前面 bank 被覆盖 |

当前 ADB runtime FDT 实际是单个 4 GiB tuple，因此上述多 bank 丢失不是本机当前 DT 的已命中根因；它仍是必须修掉的解析器缺陷，不能把未来不同布局当作安全输入。

修复版 `Devices/lancelot.dsc:17` 已将 fallback `PcdSystemMemorySize` 改为 `0x100000000`（4 GiB），并统一使用 64 位 PCD 读取；这只是 fallback/SMBIOS 一致性修复，实际成功路径仍以 runtime FDT 为准：

- `MemoryPeim()` 的成功路径依据 FDT，失败路径依据静态表，并不靠这个 PCD 自动生成正确的完整内存图。
- `PrePi.c` 已改用 `FixedPcdGet64()` 读取内存 PCD。
- SMBIOS 也从该 PCD 取容量，需要后续与实际信息一致，但这不等同于 MMU/HOB 已正确。

应修复为保存所有 bank 的范围数组，并分别构图、减去已确认的 reservation；不要用首尾拼接或已知总容量去填平内存洞。

### P2：其余应修但不能单凭它们认定本次停点的问题

1. **栈 HOB 与实际 SP（已修复）。** 修复版统一使用 stack `[0x51300000,0x51340000)`；HOB free pool top 使用 pool end，不再把栈误报成内存池尾部。
2. **fallback 不是安全兜底。** 静态图把低地址大块作为 Conventional RAM，没有 ATF/TEE/SCP/SSPM 真实保留区。并且进入 fallback 前已加过一次 Periphs，静态表又加一次，存在重复资源描述。`HLOS High` 结束于 `0x7e600000`，距 FB 起点还留有 `0x5000` 的未映射洞。
3. **FDT 保留区 reg 也只读取首组 tuple。** 加上数组边界、0 长度 reservation、区间溢出/页对齐等校验不足，不能将这个解析器视为已可靠支持所有真机内存布局；DDR 多 bank 问题见上节。
4. **MMU 错误被吞（已修复）。** `ApplyMap()`/`MemoryPeim()` 现在向上返回 `ArmConfigureMmu()` 错误；无 FDT 时拒绝不安全的静态 fallback。
5. **伪 Linux header 非标准。** 修复版仅将 Shim 的绝对复制地址改为 `0x4d100000`；其余 header 技巧仍沿用参考实现，尚未在实机 LK 路径上证明为标准 Linux header。
6. **构建脚本初始化路径过时。** `ws/firstrun.sh:7` 的 `../edk2/BaseTools` 不符合当前 `ws/edk2` 布局。这影响重新初始化环境，不解释本次已有产物的运行故障。
7. **旧 DT 分析器部分 header offset 错误。** `tools/dt_parse.py:48-54` 的 extra_cmdline 和部分 v1/v2 字段偏移不正确；现有 dump 通过 fallback 扫描仍拿到 DT 树。本审计及新增检查按真实 v2 布局独立解析，不依据其错误 header 打印判断镜像。

## 4. 最新修复版与验证顺序

以下是修复版刷入后的验证顺序；源码修复和新镜像构建已经完成。

修复版产物：`ws/boot-uefi.img`，大小 `868352` 字节，SHA-256：
`953e63a8fec6f91d569935aa141aaa3bc9eb595fcf4011985c588f1dd18571f3`。

**最新一轮 UART 非阻塞修复后产物已更新：** `ws/boot-uefi.img`，大小 `878592` 字节，SHA-256：
`9a87bf66fa9136009c7dcf1d99895404ee83843a28ea7bb692e4ccf90abd6b7a`。

**吸收 Mu-Silicium EL2→EL1 入口修复后的最新产物：** `ws/boot-uefi.img`，大小 `878592` 字节，SHA-256：
`a17047e9a4c193aff98c2cee5f2c0295a441d8d0add22689e66c2d5d8da5e741`。

不要一次把所有地址、显示参数、GIC 模式都改掉。先保存当前镜像及其哈希，每轮标明具体变更。

1. **已完成：** FDT 对齐安全读取、按 runtime reservation 解析 framebuffer、按 EL 设置异常向量、统一栈/HOB，并拒绝无 FDT 的不安全 fallback。
2. **已完成：** UART 输出改为有界非阻塞镜像；UART 未就绪不再阻塞 framebuffer/UEFI 主流程。
3. **已完成：** FD、栈、pool、vectors 已迁出 runtime reserved；构建后离线复核无重叠。
4. **刷入最新镜像后观察：** framebuffer 应继续显示阶段输出；若仍无显示，可从物理 UART 获取镜像输出，UART 不通也不会再次造成死锁。
4. **分阶段定位：** Shim 搬运前/后 → SEC 建栈后 → 安全读取 FDT 后 → MMU 开启后 → 解压 FV 后 → DXE Core → GIC/Timer → BDS/Shell。早期标记不能依赖 HOB、完整 DebugLib 或尚未验证的显示硬件初始化。
5. **已完成配置修复：** Timer physical/virtual INTID 改为 30/27；早期按 LK 已确认寄存器语义关闭 AP WDT。仍需实机验证是否进入 Shell。
6. 暂时以到达 Shell 为目标，不以启动 OS 为目标；之后再处理 MTK 存储、USB、按键和 ACPI。

尽量使用设备实际支持的临时启动路径；需要刷写时仅使用既有、明确的 boot 测试/恢复方案。本审计不要求也不建议刷写 preloader/LK/TEE 来“试地址”。

## 5. 已确认与仍需补充的实机信息（不要求 LOG）

- 已确认：刷入 boot 后启动；原厂 Redmi LOGO；随后重启；恢复原 boot 可正常启动。
- 已确认：4 GB RAM + 64 GB 存储；当前系统、LK 与固件均来自工作目录中的 DerpFest ROM。
- 待补充：Redmi LOGO 出现后大约多少秒重启，是否每次接近相同时间？
- 待补充：刷写命令、被测试文件的 SHA-256。
- 已确认运行时 DDR、TEE/ATF/SSPM/SCP/FB 的关键范围；仍需确认的是 LK 在 UEFI boot 路径下的临时 mblock 分配是否会改变候选搬运区。

这些信息用于选择上述分支；无需等 UART 日志才能先修已经明确的代码问题。

### 已取得：正常 Android 的运行时 DT

已从 root ADB 导出 LK 实际交给 Linux 的 FDT：

```sh
adb pull /sys/firmware/fdt lancelot-runtime.dtb
```

如果权限拒绝且手机**已经有 root**，可改为：

```sh
adb exec-out "su -c 'cat /sys/firmware/fdt'" > lancelot-runtime.dtb
```

文件位于 `analysis/runtime-2026-10-17/lancelot-runtime.dtb`，并已据此完成布局修复。

正常 Android 下的 DT 能提供基线；UEFI 镜像的 kernel/ramdisk 大小不同仍可能影响 LK 临时分配，不能把基线中的每个地址无条件当成 UEFI 启动时相同。
