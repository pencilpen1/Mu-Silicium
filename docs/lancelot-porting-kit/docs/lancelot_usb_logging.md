# UEFI 启动日志 → PC（USB v0.3）

## 使用的端口

| 项目 | 值 |
| --- | --- |
| 物理接口 | 手机现有 USB 数据线接口，USB 设备交给 WSL/Linux 访问 |
| USB VID:PID | `18d1:d00d` |
| USB 接口号 | `0`（fastboot vendor interface） |
| PC → 手机 | bulk OUT `0x01` |
| 手机 → PC | bulk IN `0x81` |
| 默认设备选择器 | `lancelot-uefi`，脚本可用 `--serial` 指定 |
| 日志协议 | `LLOG1`，通过 fastboot `oem log` 分段读取 |

这是 USB bulk 端点，不需要设置波特率。抓取脚本、`fastboot` 和日志分析均在
**WSL/Linux 内执行**，使用 Linux 的 USB 设备节点及访问权限。

## WSL/Linux 环境准备

需要 Python 3.9+ 和 Linux 原生 `fastboot`，Python 无第三方依赖。
Ubuntu / WSL Ubuntu 中安装：

```sh
sudo apt install fastboot
command -v fastboot
fastboot --version
```

WSL2 需要通过现有 USB/IP 透传配置，让 **UEFI 模式的 `18d1:d00d`** 设备出现在
WSL 中。Android 切换到 UEFI 会断开并重新枚举，原先 ADB 可用不代表重新枚举后
仍已透传；如抓取一直等待，请核对透传状态。Android 的网络 ADB 连接也不能代替
这里需要的原始 USB 访问。

手机进入 UEFI 后，在运行脚本的同一个 Linux 环境确认：

```sh
fastboot devices -l
```

应列出 `lancelot-uefi`。若已安装 `usbutils`，也可用 `lsusb -d 18d1:d00d` 检查透传。
若输出 `no permissions`，配置发行版的 Android udev 规则及用户组：

```sh
sudo apt install android-sdk-platform-tools-common
sudo usermod -aG plugdev "$USER"
sudo udevadm control --reload-rules
```

重新登录 Linux 会话以应用用户组，并重新连接/透传 USB 设备，使规则生效。
若 WSL 中 udev 服务未运行，需要先启用该发行版的 udev 服务。脚本遇到明确的权限错误会
退出并提示检查 udev，而不是把权限错误当成设备未连接一直重试。

## 抓取与调试

在项目根目录启动：

```sh
python3 tools/read_lancelot_log.py --serial lancelot-uefi --output uefi-boot.log
```

脚本默认从 WSL/Linux 的 `PATH` 查找 `fastboot`；若使用独立安装的 Linux platform-tools，
可加 `--fastboot /opt/platform-tools/fastboot`。
可以在手机启动前先运行脚本，再启动 v0.3 UEFI 并保持数据线连接。
脚本会等待设备，先取回保留的早期日志，再持续抓取新增日志，同时显示和保存。
按 **Ctrl+C** 结束。输出文件使用追加模式；换新文件名可区分测试轮次。

其他选项：

```sh
# 导出当前保留的日志后退出
python3 tools/read_lancelot_log.py --once -o snapshot.log

# 只写文件；每个请求超时 20 秒，空闲时每 0.5 秒查询
python3 tools/read_lancelot_log.py --quiet --timeout 20 --interval 0.5 -o uefi-boot.log

# 另一个 WSL/Linux 终端中查看或筛选日志
tail -f uefi-boot.log
grep -aEn '\[STAGE\]|ASSERT|EXC=|ELR=|ESR=|FAR=|ERR|LOST' uefi-boot.log
```

抓取期间由该脚本独占这个 USB fastboot 接口。需要手动执行其他 fastboot 命令时先停止脚本。
可以事先查询镜像版本：

```sh
fastboot -s lancelot-uefi getvar version-bootloader
fastboot -s lancelot-uefi getvar log-version
```

预期分别返回 `musb-pio-diag-0.3` 和 `LLOG1`。
旧 v0.2 仍能执行普通 getvar，但没有 `oem log`；脚本会提示更新镜像。

## 记录范围与行为

- 从 **SEC 的 `CEntryPoint()`** 开始：记录 SEC DEBUG、`Sxx / Dxx` 阶段、内存/FDT/MMU 信息、
  DXE/BDS 等经 SerialPortLib 输出的 DEBUG/ASSERT，以及 SEC 异常的 EXC/ELR/ESR/FAR。
- SEC 日志只写 RAM；USB 初始化前没有 USB 或文本控制台依赖。
- 共享内存位于 `[0x512c0000, 0x512f0000)`，192 KiB 区域中有 32 字节头部，
  实际保留 **196576 字节**。MemoryPeim 显式保留并建立不可执行的缓存映射，
  防止 DXE 分配器覆盖；地址与 FD、内存池、异常栈及捕获的运行时 reservation 不重叠。
- USB 驱动等 CPU/Timer Architectural Protocol 就绪后在 DXE 内启动，早于原先的 ReadyToBoot。
  每次读取最多 4096 字节快照，发送过程不阻塞固件日志写入。
- USB 启动后，DEBUG 主要进入 RAM/USB 通道，停止昂贵的 framebuffer DEBUG 绘制。
  GOP/Shell 图形输出和屏幕阶段标记仍独立工作，底部 USB 状态仍定时刷新。
- 当前 DEBUG mask 为 `0x80000046`；这是启用级别的日志，不包含被编译/过滤掉的 DEBUG，
  也不自动录制所有 Shell/GOP 文本。通用 DXE DebugLib 单次格式化沿用 256 字节上限，
  SEC/阶段日志格式化缓冲为 512 字节。
- 缓冲写满后覆盖最旧内容；PC 文件中会写入 `LOST ... bytes`，不会伪装成完整日志。
  同一次脚本运行中，断线重连按字节序号续取，并去除已保存的重叠数据。
- 每次启动用物理计数器值生成 boot ID，SEC 重置缓冲。脚本识别新 boot ID 后从零读取；
  这是当前启动的内存日志，不是跨重启持久化崩溃日志。
- 传输中断/超时后，脚本保留已经收到且校验通过的完整数据行。若接口不再应答，可重连数据线；
  持续写日志快于读取时，超出环形缓冲的部分仍会丢失。

**硬件限制：** BootShim/LK/preloader 的输出不在本缓冲中。若 CPU 在 USB 驱动启动前
彻底卡死，PC 无法通过尚未初始化的 USB 取走 RAM；USB 已启动后也需要 CPU、定时器和中断继续工作。
在 `ExitBootServices()` 时驱动停止 USB 通信，因此不能保证 PC 收到紧接退出前的最后几条消息。

## 线协议

PC 向 OUT `0x01` 发送 ASCII 命令，游标必须是 16 位十六进制：

```text
oem log 0000000000000000
```

IN `0x81` 上的 fastboot 响应：

```text
INFO LLOG1 <boot-id:16hex> <actual-start:16hex> <snapshot-end:16hex>
INFO LLOGD <hex-encoded-bytes>
...更多 LLOGD...
OKAY
```

上例中 `INFO` 与 `LLOG1/LLOGD` 之间的空格仅为展示分界；线上字节直接连接。
头包 60 字节；每个数据包编码至多 24 字节原始日志，总包长至多 58 字节。
这样每条响应都小于 EP1 的 64 字节包长，不依赖额外 ZLP。
fastboot 将 INFO 显示为 `(bootloader) LLOG...`；脚本去除前缀、解码十六进制，
保留原始换行、NUL 等字节，再校验长度。缓冲读取不消耗数据，游标由 PC 维护。
USB reset / SET_CONFIGURATION 取消在途快照，但不会清空共享日志。

## 产物与验证

镜像：`ws/boot-uefi.img`，874496 字节；构建日志：`ws/build-usb-log-v03.log`。

```text
SHA-256: 75a68811be558f119c0a7283b3faa4adb7388b4c3338ca70880b20ac82925054
```

已通过：

```sh
python3 tools/test_lancelot_bootlog.py
python3 tools/test_lancelot_musb.py
python3 tools/test_lancelot_log_reader.py
python3 tools/test_lancelot_fdt.py
python3 tools/test_lancelot_gop.py
python3 tools/audit_lancelot_boot.py
```

覆盖实际 C 环形缓冲、EP0 回归、USB 发送忙时等待、快照期间环形缓冲覆盖、reset、
空日志、错误命令、真实固件 INFO 数据到 PC 解码器的端到端校验、PC 分段读取/重连/
新启动/超时部分数据，以及新日志区域的内存分配与映射。
构建与宿主检查通过；**v0.3 的提前 USB 初始化和连续日志传输仍待实机验证**。
实机已验证的 v0.2 镜像保留为 `ws/boot-uefi-usb-confirmed.img`。
