# Redmi 9 / MT6768 EDK2 UEFI 移植

目标设备：**Redmi 9（lancelot），4 GB RAM / 64 GB 存储**。
平台包由 `xiaomi-blossom-dev/edk2-mt6765` 的 MT6765Pkg 移植而来，使用
EDK2 构建 AArch64 UEFI，经 BootShim 和 Android boot v2 封装生成 `boot-uefi.img`。
原系统/固件基线为 `DerpFest-v17-20260925-lancelot-Community-Stable.zip`。

## 移植到其他 UEFI

[lancelot 参数、驱动与迁移清单](docs/lancelot_uefi_porting_guide.md)：启动封装、完整固定保留区、
CPU/GIC/Timer、显示、USB、存储/按键参数、可复用驱动与待适配项。

```sh
python3 tools/export_lancelot_porting.py --output /tmp/opencode/lancelot-porting-kit.tar.gz
```

导出源码资料包，同时生成 `hardware.json` 参数索引与 `MANIFEST.sha256` 文件校验清单。

## 新增：USB 启动日志 v0.3

SEC/DXE 日志保存到共享 RAM，通过 USB 接口 0、OUT `0x01` / IN `0x81` 供 PC 读取。
配套脚本在 **WSL/Linux 内运行**，支持持续抓取、文件保存、重连续读及丢失字节提示。
使用 Linux 原生 `fastboot`，在项目根目录执行：

```sh
sudo apt install fastboot
fastboot devices -l
python3 tools/read_lancelot_log.py --serial lancelot-uefi -o uefi-boot.log
```

镜像已编译，宿主测试与离线审计通过，实机日志传输待验证。
详细使用方法及限制见 [USB 日志说明](docs/lancelot_usb_logging.md)。

## 当前进度（2026-10-17 核实）

**已达到：实机进入 UEFI Shell、正常图形显示、PC 端 USB 枚举与诊断 fastboot 查询。**
仍处于平台硬件适配阶段，尚无完整操作系统启动成果。

| 功能 | 状态与证据 |
| --- | --- |
| ROM / DTB / DTBO 分析 | 已完成基础拆包，并取得 Android root 下的运行时 FDT、内存与 USB 信息 |
| 编译与镜像封装 | 最新构建日志成功；当前镜像载荷与本地 BootShim + FD 完全一致 |
| SEC → DXE → BDS → Shell | 历史实机反馈已出现 `Shell>`，对应镜像已备份 |
| GOP 图形控制台 | 不透明像素修复后，实机反馈显示正常 |
| USB device / fastboot | v0.2 实机在 Windows 枚举为 `lancelot-uefi`，`getvar product` 成功；验证了枚举和一次 bulk 双向交互 |
| USB 完整功能 | 当前为 full-speed、PIO、定时轮询的诊断驱动；高速、持续传输、重连及休眠恢复尚未验证 |
| 输入、存储、OS | OTG host / USB 键盘、实体按键、MTK MSDC/eMMC、完整 ACPI/OS 启动尚未完成 |
| fastboot 扩展 | 已实现少量 `getvar`；download / flash / reboot、ADB 尚未实现 |

实机结论来自已有文档中的用户反馈；本次检查重新执行了宿主测试及镜像审计。

## 上一轮实机确认的修复：USB EP0 v0.2

v0.2 的源码修改集中在
[`MusbDiagnostic.c`](ws/MT6768Pkg/Drivers/MusbDiagnosticDxe/MusbDiagnostic.c)、
对应 INF 和 [`test_lancelot_musb.c`](tools/test_lancelot_musb.c)。
文件修改时间显示源码更新于当日 19:19，构建完成于 19:20，实机确认记录更新于 19:26。
根目录此前没有 Git 历史，此时间线依据文件时间、文档及镜像哈希整理。

v0.1 的现象是 Windows 无法识别设备，手机已接收到 reset / GET_DESCRIPTOR，
但未完成枚举。v0.2 修复了以下控制传输问题：

1. **共享 FIFO 交接顺序**：先 ACK SETUP，等 `RXPKTRDY` 清零并读回确认，再写入 IN 数据；等待有界且不阻塞主流程。
2. **状态阶段完成判断**：尊重 `DATAEND`，防止因旧中断过早应用 `SET_ADDRESS`。
3. **连续请求处理**：兼容控制器自动消费 OUT 状态零长度包，以及状态完成和下一 SETUP 合并到来的情况。
4. **寄存器与调度**：COUNT0 改为 8 位读取；USB 轮询提升到 `TPL_NOTIFY`，减少屏幕回调拖延。
5. **诊断与回归测试**：增加 EP0 状态、CSR、ACK 超时和完成计数；覆盖延迟/卡住的 ACK、FIFO 交接及状态阶段。

已有实机复测确认这一组修改解决了原先的枚举失败，并可执行 `fastboot getvar product`。
各项修改未分别做实机对照测试。细节见 [USB 适配记录](docs/lancelot_usb_bringup.md)
及 [驱动说明](ws/MT6768Pkg/Drivers/MusbDiagnosticDxe/README.md)。

## 此前的重要修复

- **启动内存布局**：FD 从与 TEE reservation 重叠的 `0x70000000` 搬到 `0x4d100000`，同步修正内存池、栈及异常栈。
- **早期启动与内存**：FDT 对齐安全读取和边界校验；修正 resource / allocation HOB，避免把全部可用 RAM 或整个内存池标记为已占用；拒绝不安全 fallback。
- **架构交接与诊断**：修正 EL2→EL1、异常向量、cache 同步及 timer INTID，处理 AP watchdog，增加独立 SEC/DXE 阶段与异常屏幕输出。
- **显示**：采用运行时 framebuffer `0x7cdb0000`、1088 像素步幅；移除导致 ANSI 转义文本显示的串口控制台路径；修正不透明像素、BLT 向下复制越界及脏行 cache 刷新。

旧文档按迭代追加，仍保留“未进入 Shell”“USB 不可用”和旧地址等历史表述。
阅读进度时优先参考本页及 USB 文档顶部的实机确认，再查
[屏幕审计](docs/lancelot_screen_audit_2026-10-17.md)、
[启动审计](docs/lancelot_boot_audit_2026-10-17.md)、
[最初数据表](docs/lancelot_porting_datasheet.md)。

## 当前产物

`ws/boot-uefi.img` 为 USB 日志 v0.3，**874496 字节**：

```text
SHA-256: 75a68811be558f119c0a7283b3faa4adb7388b4c3338ca70880b20ac82925054
```

实机确认的 v0.2 仍保留在 `ws/boot-uefi-usb-confirmed.img`（哈希见同名 `.sha256`）。
另有 Shell、显示及 USB v0.1 镜像备份。镜像和构建日志在本地保留，由 `.gitignore` 排除；
现有 `.sha256` 文件可纳入版本管理。

## 目录说明

| 路径 | 用途 |
| --- | --- |
| `ws/MT6768Pkg/` | 本项目平台源码：PrePi、内存初始化、GOP、控制台、MUSB、ACPI 等 |
| `ws/BootShim/` | LK 到 UEFI 的 AArch64 入口和 FD 搬运代码 |
| `ws/build*.sh`、`ws/mkbootimg.py`、`ws/blobs/` | 构建、封装脚本及 DTB / initramfs 输入 |
| `tools/` | ROM 提取、DT 分析、USB 信息采集、离线审计和宿主回归测试 |
| `docs/` | 移植取数、故障分析、验证记录及依赖版本 |
| `analysis/` | 静态 DT dump 和实机采集；有效 USB 采集为 `usb-2026-10-17-root/`，无 root 后缀的旧采集含命令错误 |
| `patches/` | 保存外部 EDK2 仓库中本地 DXE 屏幕诊断修改 |
| `ws/edk2/`、`ws/edk2-platforms/`、`refs/` | 外部依赖及参考仓库，根仓库忽略这些 checkout |
| `ws/workspace/`、`rom-extract/` | 构建输出、ROM 分区提取物，根仓库忽略 |

## 构建与检查

依赖版本、外部 EDK2 补丁和首次构建步骤见 [依赖说明](docs/dependencies.md)。
当前已配置好的环境从 `ws/` 构建：

```sh
cd ws
./build.sh
```

在项目根目录执行现有检查：

```sh
python3 tools/test_lancelot_fdt.py
python3 tools/test_lancelot_gop.py
python3 tools/test_lancelot_musb.py
python3 tools/test_lancelot_bootlog.py
python3 tools/test_lancelot_log_reader.py
python3 tools/audit_lancelot_boot.py
```

本次全部通过：16 组 FDT 用例；MemoryPeim 生成 41 个资源 HOB、3 个分配 HOB、
22 个 MMU 映射；GOP 与 MUSB 实际 C 代码通过 ASan/UBSan；镜像载荷、入口、
timer 和捕获运行时保留区检查一致。审计脚本需要本地 ROM boot、编译输出和运行时 FDT。

后续优先实机验证 v0.3 启动日志抓取和重连续读，再推进输入、存储及 OS 所需的平台支持。
