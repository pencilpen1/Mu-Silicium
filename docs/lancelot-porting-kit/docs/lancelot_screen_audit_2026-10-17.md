# lancelot 绿色停点复核（AUDIT2）

## GOP-only 实机反馈与不透明像素诊断版

GOP-only 镜像用户反馈无可见 Shell 提示符，最后是
`InstallProtocolInterface: 6302D008-7F9B-4730-87AC-60C9FEF5DA4E ...`。
源码中的 Shell Protocol GUID 为 `6302D008-7F9B-4F30-87AC-60C9FEF5DA4E`，
中段存在抄录差异；若指此 GUID，说明已进入 Shell 初始化，不能用最后日志认定死锁。
用户确认 UEFI USB 不可用，没有键盘输入。

检查发现 GOP 转换/填充输出 reserved byte 为 0，而可见 framebuffer DEBUG 字符为 FF。
LK 若保持逐像素 alpha 混合，前者可能透明；尚未读显示控制寄存器，属于待验证假设。
本轮 SimpleFbDxe 对成功写入的目标矩形强制最高字节 FF，SetMode/初始化清屏采用
不透明黑，并只清理目标行 cache、在 RestoreTPL 前发布写入。
同时修复 BLT 向下重叠复制从 `Height` 而不是 `Height-1` 行开始的越界，
GraphicsConsole/ConSplitter 的 mode 0 恢复标准 80x25。

BDS 显式连接物理 GOP 并直接显示 `GOP CONNECT`、`GOP TEXT` 状态；
AfterConsole 输出 `Lancelot GOP console test - no keyboard required`，显示
`CON WRITE` 与 `CON MODE`。状态值 0 为成功，但虚拟 ConOut 成功不保证有物理输出。
后续 GOP ClearScreen 可能清除状态行，必要时拍开机过程。

验证：`python3 tools/test_lancelot_gop.py` 编译实际 GOP BLT adapter 和 BLT 库，
ASan/UBSan 检查 RGB/BGR、最高字节、stride padding、上下重叠复制、目标行 flush、
TPL 恢复和非法目标矩形通过。构建及 `tools/audit_lancelot_boot.py` 通过。
最新 `ws/boot-uefi.img`：864256 字节；日志 `ws/build-gop-opaque.log`；SHA-256：
`85b6726592bd2b4c391f0618c975f12645a68de7c41b4370d52d045eb60f8309`。
本版显示效果待实机验证；已确认 Shell 版本备份仍保留。

## 实机进展：已出现 Shell 提示符

用户测试 AUDIT2 后反馈顶部黑色，最后一行出现带 `[1m`、`[33m` 等字符的
`Shell>`。这表明 Shell 已运行到提示符，SEC→DXE→BDS→Shell 链路已通过；
尚不能据此确认键盘输入、存储或 OS 启动可用，也不能单独归因于本轮某一项修复。
已将该镜像保存为 `ws/boot-uefi-shell-confirmed.img`，SHA-256 为
`8bca02afbc65436e031a552d9a4fc3140980f8a465ffe1bec1d1819c0190fdc4`。

显示控制序列的原因：`PlatformBootManagerBeforeConsole()` 同时注册 GOP 和串口终端；
SerialDxe 实际绑定 framebuffer SerialPortLib，后者丢弃 ESC 字节却绘制后续 ANSI 文本。
已从 DSC/FDF 移除 SerialDxe、TerminalDxe，并从 ConIn/ConOut/ErrOut 删除旧串口路径。
Shell 使用 GOP/GraphicsConsole；DEBUG 仍可使用 framebuffer 日志库。

本次显示修正版：`ws/boot-uefi.img`，864256 字节；日志 `ws/build-gop-console.log`。
SHA-256：`e747629379a89dbc1dbb0ef3727479c078824ea22e5f048ffb6b0e1e7172ad7c`。
构建和离线载荷审计通过；显示修正版待实机确认。以下为 AUDIT2 时的审查记录。

## 结论与证据边界

用户无串口，多次测试仅报告“顶部绿色”。此前将此认定为卡在
`UartInit()` / 第一条 DEBUG / 34 MiB cache flush，证据不足，撤回该定位。

1. 旧 `PrePi.c` 和 `DxeMain.c` 都使用 `0xff00ff00`：分别表示 SEC 进入、
   DXE Debug Agent 完成。最终绿色也可能是 DXE 内存服务之前的标记。
2. `SecScreenStage()` 先写像素、再执行 cache maintenance；看见绿色不能证明
   该函数已返回，更不能证明后续 console 初始化已执行。
3. 实际反汇编的 `SerialPortInitialize()` 是状态保存、内存赋值和返回，没有 UART
   访问或轮询。它不能被静态认定为硬件阻塞根因。
4. 此轮确认的是具体代码缺陷和诊断缺陷；实际卡死指令仍需新的屏幕编号/异常寄存器定位。

## 本轮已修改

| 问题 | 文件/处理 | 对当前停点的判断 |
| --- | --- | --- |
| SEC/DXE 重复颜色，文本输出覆盖标记 | `Include/Library/BootScreen.h`：独立 S/D 编号行；console 保留前 384 像素行 | 确定诊断错误 |
| DXE trace 仍读 UART MMIO，即使删除了 SerialPort 镜像 | `edk2/MdeModulePkg/Core/Dxe/DxeMain/DxeMain.c` 删除 UART trace | 总线阻塞仅为候选，未实机证实 |
| SEC 调试与断言依赖文本 console | `SecScreenDebugLib` 抑制常规 DEBUG，但保留屏幕 ASSERT 文件/行号；SEC SerialPort 使用标准 Null 库 | 诊断隔离，不代表文本就是根因 |
| DXE 早期异常文本在构造函数运行前可能被丢弃 | `DxeMain.c` 在 D00 后初始化 framebuffer console | 让早期异常/ASSERT 有输出机会 |
| 整个 PrePi pool 被记为已分配，覆盖真实页分配 HOB；栈重复分配 | `MemoryInitPeiLib.c`：pool 仅发布资源；实际页分配由 PrePi allocator 记录；栈由 `BuildStackHob()` 记录 | 确定 HOB 语义错误，影响 DXE 内存/GCD |
| 所有 Conventional RAM 生成分配 HOB | 同上：可用 RAM 仅发布 resource HOB | `FindLargestFreeRegion()` 会把这些 HOB 视为占用，限制 DXE 寻找空闲区域 |
| TEE/固件 reserved 被映射为普通可缓存 RAM | reserved 只生成资源 HOB，不建立 CPU 映射；framebuffer 单独保留映射 | 防止非必要访问/推测访问固件保留区 |
| FDT 无界扫描/节点名溢出/深度溢出/静默丢弃 reservation | 限定块、字符串和节点边界；检查 tuple 和容量、地址溢出；不支持的多 bank 明确拒绝 | 捕获 DTB 可通过；没有证据说用户 DTB 曾损坏 |
| framebuffer flush 行号单位错误 | `FrameBufferSerialPortLib.c`：字符行乘 `FONT_HEIGHT`；无换行的最后一批文字也 flush | 上一轮“缩小 flush”引入的确定错误 |
| 换行跳过 intstate 初始化，可能错误启用 IRQ | 在控制字符分支前读取 intstate；移除重绘 newline 的回跳 | 确定未初始化变量路径 |
| 32 位 framebuffer 的滚屏使用 16 位指针 | 改为 UINT32，保留诊断区 | 潜在显示错误；该函数不是已证实停点 |
| EL2→EL1 仅写 HCR/SPSR/ELR | `ModuleEntryPoint.S` 定义 SCTLR_EL1 关闭 MMU/cache 状态，开放物理 timer/counter、解除 FP trap，补 ISB；入口屏蔽 DAIF，选择 SP_EL1 | 修复不完整的架构交接；实际入口 EL 待测 |
| MMU 前启用 D-cache，随后对正在运行的 FD 作 invalidate-only | D-cache 由 `ArmConfigureMmu()` 开启；删除对活动 FD 的 invalidate-only | 避免丢弃运行中数据状态 |
| SEC 异常依赖 console，只有 ELR 且误称 LR | 独立紧急栈、直接屏幕显示 EXC/ELR/ESR/FAR，递归保护 | 只处理致命异常，不恢复执行；不保证 framebuffer 本身故障时仍可显示 |
| BootShim 复制代码后直接跳转 | 加 DSB / I-cache invalidate / ISB | 仍按 Linux 入口 ABI 假设 MMU/D-cache 已关闭 |
| GOP QueryMode 缺少模式/参数/分配失败处理 | `SimpleFbDxe.c` 增加检查及完整 PixelInformation；只暴露单个可见 surface | 后续显示驱动问题，不证明导致 SEC 卡死 |
| BLT mask 字节数少算部分位宽、shift[3] 未初始化 | `FrameBufferBltLib.c` 修正位宽公式及数组初始化 | 常见 32 位路径原公式恰好仍为 4，不是本次根因证据 |

`Library/UartSerialPortLib` 的遗留写入已停用；生效配置只在 DXE 使用 framebuffer 库。
历史 `DeviceMemoryMap.h` 静态表仍保留，但标明不可用，实际不会回退到其 TEE 重叠布局。

## 新屏幕标记

顶部第一行是 **Sxx**，第二行是 **Dxx**；不再靠颜色判断。
首次进入 SEC 后第二行先显示 `AUDIT2 - DXE NOT ENTERED`。
S/D 更新互不覆盖，普通日志从像素行 384 开始。GOP/图形控制台接管后可能清屏。

| 标记 | 已完成 / 下一步 |
| --- | --- |
| S01 | C 入口已进入，下一步停 AP watchdog |
| S02 | watchdog 停用完成，进入 Main |
| S03 | 即将构造 HOB |
| S04 | HOB 指针就绪，进入 FDT/内存图/MMU |
| S05 | 内存 HOB/MMU 完成 |
| S06 | 平台 HOB 完成，运行 SEC 库构造函数 |
| S07 | SEC 库构造完成，准备解压 FV |
| S08 | FV 解压完成，查找/加载/跳转 DXE Core |
| D00 | DxeMain 进入，随后初始化早期 console 与异常向量 |
| D01 | DXE 异常处理初始化返回 |
| D02 | Stack Guard 阶段结束 |
| D03 | Debug Agent 返回，下一步 `CoreInitializeMemoryServices()` |
| D04 | DXE 内存服务返回，随后系统表/镜像服务/GCD |
| D05 | GCD 返回 |
| D06 | DXE 库构造完成 |
| D07 | 即将调度 DXE 驱动 |
| D08 | DXE Dispatcher 返回 |
| D09 | 即将进入 BDS |

MEM ERR / FV ERR / DXE RET 表示明确返回失败；ASSERT 给出文件名及十六进制行号；
SEC EXC 后的 ELR 是异常返回 PC，ESR 是原因，FAR 在部分异常类型下无效。
DXE 安装自己的向量后，异常由 EDK2 的 handler 输出到屏幕日志区。
标记函数本身也可能异常，编号不是“对应函数返回”的无条件证明。

## 已验证

最终镜像：`ws/boot-uefi.img`，874496 字节，构建日志 `ws/build-screen-audit.log`。
SHA-256：`8bca02afbc65436e031a552d9a4fc3140980f8a465ffe1bec1d1819c0190fdc4`。
该版本未做实机启动验证。

- `python3 tools/test_lancelot_fdt.py`：编译**实际 C 解析器**，16 组捕获/合成 DTB
  用 ASan/UBSan 检查：真实 DDR/TEE/FB、深节点、长名称、畸形偏移/长度、缺失
  reservation 终止、未终止字符串、超出容量、多 tuple、多 bank 拒绝。
- 同一命令编译**实际 MemoryPeim**，mock MMU/HOB 接口：捕获 FDT 产生
  **41 个资源 HOB、2 个固定占用 HOB、22 个 MMU 映射**；没有整池分配 HOB；
  栈分配留给 PrePi 主流程；TEE 不进入页表。所有区域保持页对齐。
- AArch64 DEBUG 构建成功；仍有工具链原有 RWX segment / Python escape 等警告。
- `tools/audit_lancelot_boot.py` 检查 boot v2 header、载荷与当前 Shim+FD 一致、
  reset branch 与 Fv.map 入口一致、实际重定位后 VBAR 对齐及地址常量、运行时
  reservation 与 FD/pool/栈不重叠、timer INTID 配置。
- 反汇编确认 Fdt32 实际使用 `ldrb`，SEC 编译带 `-mstrict-align -mgeneral-regs-only`。

## 仍需实机或后续适配

1. 真正停点、当前 EL、异常原因尚未读取；本轮不宣称已启动 Shell。
2. 固定 framebuffer/栈/FD 仍以捕获的 Android runtime FDT 为依据。当前 UEFI 启动时
   LK 的布局若不同，早期写入发生在 FDT 校验前；动态检查无法事后保护首次搬运。
   新解析器会拒绝与显示配置不兼容的 FB 描述，但不会自动重编 PCD。
3. LK 必须提供可读且完整的 FDT；软件边界检查不能验证任意无效物理指针。
   当前仅支持该机实测的单 bank、2/2 cell 布局。
4. 显示控制器/扫描地址/像素通道顺序仍由 LK 保持；颜色不能证明 RGB/BGR 顺序。
   没有完整 MTK 显示控制器驱动。GOP 与 framebuffer DEBUG 同时写屏可能相互覆盖。
5. EL1-only 启动由更高 EL 提供 timer/GIC 系统寄存器访问权限；UEFI 无法在 EL1
   修复被 EL2/EL3 trap 的访问。GICv3 真实中断、PSCI、PMIC watchdog 仍需实机验证。
6. BootShim 仍沿用 LK 专用的 fake Linux header（text_offset 作为绝对复制地址），
   不是通用 ARM64 Image；未将其跨 bootloader 兼容性当作已验证。
7. 后续 BDS 等待输入、串口短路径、ACPI/SMBIOS 的参考平台残留、eMMC/USB 控制器/
   实体按键缺少完整适配，不能由打包通用驱动推导硬件可用。
   `PlatformBootManagerWaitCallback()` 的 Timeout=0 除法是潜在路径；正常零超时流程
   通常不调用此回调。它不能解释尚未定位的绿色阶段。
8. 当前 DXE Core 的屏幕探针直接依赖 MT6768Pkg，是本地诊断改动，不是可上游的通用实现。
