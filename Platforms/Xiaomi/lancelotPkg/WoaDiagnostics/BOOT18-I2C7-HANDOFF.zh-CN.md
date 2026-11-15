# boot18 I²C7 轮询交接

boot17 原日志中控制器 START=0、CONTROL=0xA8、DMA_EN=0，
但 IFR2 非 AP 的 I²C 门未关闭，MCU_INTR=3、INTR_MASK=0x1F8。
CCFG=0x10000/reason=6，因此 Windows 诊断没有进行芯片读取。

本次仍只允许 CCFG 0x10031 的诊断契约，保持 LCH0001、64 字节 NVS、
原 LBAT/PWRAP 所有者、八核与完整 RAM。所有 ACPI 表逐字节保持 boot17。

实现先保持 I²C_AP/APDMA 和实际 I²C 父钟，再观察 10 ms 连续空闲。
START、DMA_EN 或 CONTROL.DMA_EN 忙时拒绝交接，不复位、不清除请求。
通过 IFR2 W1S 地址 0x10001080，仅关闭 bit12/13/14 的 CCU/SSPM/reserved
I²C 时钟路径，回读 0x10001090 并再次观察空闲。这里隔离的是非 AP I²C
路径，不关闭 SSPM 本身，也不改变共享 I²C mux/PLL。

不能用 Clock->SetEnable(FALSE) 代替局部门控：当前 ClockDxe 的 Gate、Mux、
Factor 路径会递归关闭父钟，影响同源的 AP 和其他外设。
时钟地址/位定义来自当前 MT6768 ClockImplLib 的 INFRA_I2C_CCU/SSPM/RSV。
lancelot Android i2c7 节点没有 ch_offset_default/ch_offset_ccu；CCU 多通道
只配置在其他 I²C 节点。WOA 构建保持 LANCELOT_USB_HOST=0，未包含后续
MusbHostDxe/Type-C I²C7 客户端。将来新增客户端必须重新设计共享交接。

仅在隔离和空闲验证后，局部设置 GPIO175/176 为 function 1，屏蔽 GIC
INTID142 和 EINT20，再将 I²C7 INTR_MASK、MCU_INTR 设为 0 并回读。
Android i2c-mtk v2 在启动事务前写 MCU_INTR_EN；它是中断使能/路由配置，
不能作为“仍有 MCU 占有”的证据。此过程没有 I²C START/FIFO/状态清除、
控制器或 DMA 复位，也没有 SMB1351 事务及充电配置写入。

ExitBootServices 始终重采样，包括准备失败的情况。controller_valid 区分
未开钟的寄存器与实测零值；如果先前开放的契约不再满足就撤回诊断位。
新增 BEFORE_ISOLATION、NON_AP_ISOLATED 和 EBS 的实际状态日志，
INTR_STAT/FIFO_STAT 只读。Windows 必须依据下一轮真实芯片读数验收，
本变更本身不证明充电已经正常或保护/USB 授权已经完成。

从独立源码快照构建：

```sh
python3 Platforms/Xiaomi/lancelotPkg/WoaDiagnostics/BuildWoa.py --profile smp --board-profile all --output /mnt/d/AI_rom/WOA/lancelot-bringup/artifacts/firmware-boot18-buttons-charger
```
