# MUSB peripheral diagnostic (lancelot)

This copy is integrated into the main Mu-Silicium lancelot build as
`MediaTekPkg/Drivers/MusbDiagnosticDxe/MusbDiagnosticDxe.inf`. It is enabled
only for lancelot and produces a full-speed PC-facing diagnostic device.

The default dual-role profile uses `MusbDiagnosticDualRoleDxe.inf`, which builds
this same packet engine with `MUSB_MANAGED_ROLE`. It registers start/stop callbacks
with the Host/Type-C role manager and waits for PC ownership before accessing the
MAC. Stop cancels the poll timer; reconnect can restart it. The standalone INF
remains available through `LANCELOT_USB_HOST=0`. In managed mode the shared boot
log is retained across role switches. Regression tests cover both variants.

The current build initializes the shared DXE boot-log ring; SEC-side producers
are not part of the main Mu-Silicium platform yet. The `oem log` protocol remains
available. The bottom-of-screen overlay and its repaint timer have been removed.

On 2026-10-18 the user reported normal `fastboot devices` output with the Mu
telemetry image. This confirms device discovery; a new successful bulk query
has not yet been reported for this Mu build. Host-mode preparation is tracked in
[the host bring-up notes](../../../../../docs/lancelot_usb_host_bringup.md).

Experimental automatic PC-facing bring-up driver. Starts during DXE after the
CPU/timer protocols are available and stays active while Shell runs. It does not publish a generic USB controller
protocol and does not use the existing AndroidFastboot transport/application.

## v0.3 boot log transport (host-tested; phone test pending)

`oem log <16-hex-cursor>` reads up to 4096 bytes from the shared DXE ring.
Fastboot INFO packets carry a boot ID, actual start/end sequences and hex-encoded
log bytes. All packets are shorter than 64 bytes; TXPKTRDY provides backpressure.
The snapshot is immutable while being sent. Reset/unconfigure cancels the reply
but preserves the ring. Existing getvar queries remain available.

PC script: `docs/lancelot-porting-kit/tools/read_lancelot_log.py`; wire protocol:
[`lancelot_usb_logging.md`](../../../../../docs/lancelot-porting-kit/docs/lancelot_usb_logging.md).
That archived guide describes the original EDK2 producers; this Mu build starts
logging when the USB DXE driver starts.
The implementation is v0.3 (`bcdDevice=0x0003`), with `getvar log-version=LLOG1`.
The hardware validation below applies to the preserved v0.2 baseline.

## Hardware validation

User confirmed v0.2 on lancelot: Windows `fastboot devices` lists
`lancelot-uefi`, and `fastboot getvar product` returns `product: lancelot-uefi`
(host-reported total time 0.001 s). This validates enumeration and one bulk
command/response, not throughput or general-purpose USB support. Tested image:
`ws/boot-uefi-usb-confirmed.img`, SHA-256
`0a801f24df8e541b6522b6b315ab1849728a1210ac52b21008c19ab5039e3720`.

## v0.2 EP0 correction

v0.1 reached Windows reset/GET_DESCRIPTOR, but enumeration failed. User screen:
stage 4, status 0, reset/setup `0000000800000007`, request `80060100`,
HW/DEVCTL `0000e50300000099`, no bulk commands. This establishes received USB
traffic, not completed descriptor transfers. DEVCTL 0x99 reports B-device,
valid VBUS and SESSION, with HostMode clear.

The IN response now acknowledges SETUP **before** writing the shared EP0 FIFO,
and waits for RXPKTRDY-clear readback in subsequent poll callbacks (bounded to
100 callbacks). This follows the ordering in MediaTek `musb_read_setup()`.
Previously the FIFO was written while RXPKTRDY still held SETUP, and one CSR
write combined receive ACK with TXPKTRDY/DATAEND. That ordering is a concrete
difference from the reference implementation. The v0.2 hardware retest passed;
this change was not isolated from the other EP0/polling fixes below.

Status processing respects DATAEND; SET_ADDRESS waits for status completion.
Controller-consumed status OUT ZLPs and coalesced status/SETUP events are handled.
COUNT0 is read as 8 bits. FIFO byte accesses remain: the reference explicitly
supports them. USB polling uses TPL_NOTIFY. The original v0.2 used
bcdDevice=0x0002; getvar version-bootloader reported 0.2.

`python3 Resources/Scripts/test_lancelot_musb.py` exercises the integrated C state machine with
ASan/UBSan and a stricter mock: FIFO writes fail if receive ownership or the
ACK readback is outstanding, delayed/stuck ACKs are tested, and DATAEND remains
set until a simulated status handshake. This is a software-ordering regression
test, not an electrical/controller emulator.

## Supported test

- USB **full-speed (12 Mbit/s)** enumeration, EP0 64-byte packets.
- VID/PID `18d1:d00d`, vendor interface `ff/42/03` (fastboot), EP1 IN/OUT 64 bytes.
- Serial descriptor `lancelot-uefi` is a diagnostic identifier, not the hardware serial.
- `fastboot devices`, `fastboot getvar product`, `fastboot getvar version`,
  `fastboot getvar version-bootloader`, `fastboot getvar serialno`.
- `fastboot getvar log-version`, `fastboot oem log <16-hex-cursor>` (v0.3).
- Other commands return FAIL. No flash, download, reboot, ADB, mass storage,
  OTG host, or USB keyboard support in this diagnostic image.
- PIO with 1-ms UEFI timer polling; no DMA/IOMMU buffer dependencies.
- On ExitBootServices, stop touching hardware and drop D+ pullup.

The purpose is to validate PHY + enumeration + bidirectional bulk I/O first.
Full controller protocol integration follows actual hardware verification.

## Hardware assumptions and remaining validation

LK must leave UNIVPLL and VUSB/VIO18/VA12 rails usable. This driver does not
reprogram shared PLL frequency or access PMIC/Type-C I2C to source VBUS.
These inherited conditions were sufficient in the successful v0.2 device test.
It enables the USB mux/gate, restores the USB2 PHY from suspend/UART state,
forces peripheral-role signals, disables legacy DMA/QMU and uses indexed EP CSRs.
An absent UNIVPLL enable bit returns EFI_NOT_READY before touching MAC/PHY.
No software timeout can recover a bus access that never completes.

The cable should be attached to a PC. Role signals are fixed to device mode;
hotplug/Type-C policy and suspend power management are not implemented yet.
The output is not proof of electrical/USB compliance or completed USB support.

## Logging

The driver does not draw on the framebuffer. Initialization and bus-reset logs
are still buffered for `oem log`. EP0/EP1 state counters remain internal; removal
of the overlay keeps the current descriptor, FIFO, polling and PHY sequences.

## Source evidence

Android capture: `analysis/usb-2026-10-17-root/` (kernel ShockwaveKernel-S+
4.19.325-cip136-st20). Exact source revision of the running build is not known.

Register/sequence reference repositories (GPL-2.0):

- `MrShockWAVEog/android_kernel_xiaomi_mt6768-r`
  commit `3d974afe57c2431145debf1c788bd2cc7676e0b2`:
  `drivers/misc/mediatek/usb20/mtk_musb_reg.h`, `musb_core.c`,
  `musb_gadget_ep0.c`, `mt6768/usb20_phy.c`, `drivers/clk/mediatek/clk-mt6768.c`.
- `mt6768-dev/android_kernel_xiaomi_mt6768`
  commit `9aa3fdfd82e94fcf7ed8fc5a8458e44dd9c479a4`:
  `drivers/phy/mediatek/phy-mtk-tphy.c` (MT6768 branches).

Temporary reference copies: `/tmp/opencode/lancelot-usb-source/` and
`/tmp/opencode/lancelot-usb-modern/`. These references are not asserted to be the
exact source used to build the installed ROM.
