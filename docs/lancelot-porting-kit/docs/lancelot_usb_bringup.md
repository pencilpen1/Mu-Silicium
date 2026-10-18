# lancelot USB bring-up

## Current build: v0.3 buffered boot logs

Added SEC/DXE shared-RAM logging and `oem log <16-hex-cursor>` over the existing
USB interface 0 / EP01 OUT / EP81 IN. USB now starts during DXE after CPU/timer
protocols are available. PC reader: `tools/read_lancelot_log.py` (Python + fastboot).
Build, host regression tests and image audit passed; v0.3 requires a phone test.
Setup, protocol, image hash and limits: [USB boot logging](lancelot_usb_logging.md).

## Confirmed: v0.2 enumeration and bidirectional bulk query

User tested v0.2 on the Windows PC and reported:

```text
PS C:\Users\26712> fastboot devices
lancelot-uefi    fastboot
PS C:\Users\26712> fastboot getvar product
product: lancelot-uefi
Finished. Total time: 0.001s
```

This verifies PC-facing enumeration and one fastboot command/response over
bulk OUT/IN on the tested boot/cable setup. EP0 v0.2 fixes resolved the observed
enumeration failure; the individual ordering, DATAEND and polling-priority changes
were not tested separately. It does not establish sustained throughput, reconnect
reliability, suspend/resume, high-speed mode or OTG host support.

Verified image preserved as `ws/boot-uefi-usb-confirmed.img`, 868352 bytes,
SHA-256 `0a801f24df8e541b6522b6b315ab1849728a1210ac52b21008c19ab5039e3720`.
`ws/boot-uefi.img` had the same contents at that milestone; it now contains v0.3.
The preserved v0.2 image is the diagnostic
fastboot implementation: getvar queries are supported; flash/download/reboot,
ADB and USB host keyboard support have not been implemented.

The sections below retain the earlier investigation and test history.

## Update: v0.1 reaches GET_DESCRIPTOR; EP0 v0.2 repair

User reports Windows cannot identify the UEFI USB device. Screen values:
`STAGE=4`, `STATUS=0`, `RESET/SETUP=0000000800000007`,
`REQUEST=80060100`, `COMMAND=0`, `HW/DEVCTL=0000e50300000099`.
These indicate eight bus resets and seven parsed device-descriptor SETUPs.
No SET_ADDRESS completion/configuration/bulk exchange is established. The last
request does not prove what Windows received. 0x99 is consistent with device
mode/VBUS valid; no evidence yet that a Windows interface driver is the blocker.

Reference comparison identified the IN FIFO ownership transition missing from
v0.1: ACK SETUP, wait for RXPKTRDY=0, then load/send TX. v0.2 implements this
as a bounded non-blocking phase, checks DATAEND before completing a status
transfer, handles auto-consumed OUT status and coalesced next SETUP, reads
COUNT0 as an 8-bit register, and raises only the lightweight USB polling callback
to TPL_NOTIFY. PHY/clock initialization and FIFO access width were kept for this
retest; kernel source explicitly supports byte FIFO accesses.

Additional telemetry: request wIndex/wLength, last active/queued CSR0, EP0
queued-packet/completed-request counters, current control state and ACK/stall
errors. Values and bitfields are documented in the driver's README.

The host regression test now rejects FIFO writes before RX release/readback,
tests delayed/stuck release and stale IRQs with DATAEND high, and checks both
automatic OUT status completion and back-to-back SETUP. ASan/UBSan passed.
This does not prove the hardware failure is resolved; v0.2 needs a phone test.

v0.2 build and payload audit passed: `ws/build-musb-ep0-v02.log`;
`ws/boot-uefi.img`, 868352 bytes, SHA-256
`0a801f24df8e541b6522b6b315ab1849728a1210ac52b21008c19ab5039e3720`.
v0.1 is preserved as `ws/boot-uefi-usb-v01.img` (hash `1a40f957...1f4a`).
Next test: boot v0.2, reconnect the PC cable, then `fastboot devices` and
`fastboot getvar product`. If still failing, capture all lower-screen USB2
telemetry, especially IDX/LENGTH, CSR/QUEUED, EP0 TX/DONE and ACKERR/STALL.

## Update: live capture and first peripheral diagnostic build

ADB access was restored; device `116745f30503` is authorized and `su` reports
uid 0 (KernelSU). Valid capture: `analysis/usb-2026-10-17-root/` and corresponding
ZIP. The earlier `analysis/usb-2026-10-17/` attempt contains shell-quoting errors
and must not be used as hardware evidence; the collection script was corrected.

Live capture confirms `musb-hdrc` configured at high-speed, role `device`, Type-C
sink, `USB=1 / USB-HOST=0`. USB MAC binds `mt_usb`, PHY binds `mtk-tphy`, extcon
binds `mtk-extcon-usb`. I2C 7-0055 binds **smb1351-charger**, and 7-0060 binds
**usb_type_c**. Thus the legacy MT6370 DT nodes are not sufficient evidence for
programming OTG VBUS. Kernel config enables USB_MTK_HDRC, MTK_MUSB_DUAL_ROLE,
PHY_MTK_TPHY, TCPC_WUSB3801 and SMB1351_USB_CHARGER. Clock/regulator debug summaries
are unavailable, recorded as missing files, not successful captures.

Fresh FDT SHA-256: `f3b3d062e0acecb1c6ebcf2e3490cf6d5f811ce21be3f6a12e6f21a380f2e30a`.
FD/pool/vector/stack ranges were rechecked against its reservations: no overlap.

User selected PC connection first. Added `MusbDiagnosticDxe` with automatic
ReadyToBoot startup, full-speed EP0 enumeration, polled PIO bulk diagnostic
fastboot `getvar` and screen status. Detailed assumptions, source references,
limits and stage numbers are in `ws/MT6768Pkg/Drivers/MusbDiagnosticDxe/README.md`.
This first version relies on LK's shared PLL/power rails; it has not been tested
on the phone. It is not a general USB-device protocol driver, ADB service, host
driver or flashing implementation.

Build: `ws/build-musb-diag-final.log`, success. Host test:
`python3 tools/test_lancelot_musb.py`, ASan/UBSan with mocked MMIO, passed:
descriptor lengths, delayed address, configure/unconfigure, endpoint halt,
EP0 ZLP, setup abort/reset, diagnostic bulk replies and ExitBootServices detach.
These tests do not emulate PHY/electrical behavior or prove register sequencing.
`tools/audit_lancelot_boot.py` confirms packaging/reset vector/payload integrity.

Image: `ws/boot-uefi.img`, 868352 bytes.
SHA-256: `1a40f9576e98487697808fa8d80e8d9e59533b4537b8a602c6973331f6a11f4a`.
Working display backup: `ws/boot-uefi-display-confirmed.img`, SHA-256
`85b6726592bd2b4c391f0618c975f12645a68de7c41b4370d52d045eb60f8309`.

Next physical test after booting the new UEFI image with a PC cable attached:
`fastboot devices`, `fastboot getvar product`, `fastboot getvar version`.
Expected diagnostic serial/product: `lancelot-uefi`. Check screen bottom USB
STAGE/STATUS and reset/setup counts if enumeration fails. Windows may need its
Android Bootloader interface driver for VID/PID 18d1:d00d. ADB is not expected in
this UEFI image. No device reboot or flashing was performed by this session.

The remainder below records the initial investigation/requirements.

## Confirmed state

User confirms the latest opaque-GOP image displays normally. USB is not usable;
the phone is now in Android with root ADB and connected to the Windows PC.
The coding session is in WSL2: native `adb devices -l` is empty, USB sysfs contains
only root hubs. Windows executable interop currently returns Exec format error.
This is an access limitation for diagnostics, not evidence of an Android USB fault.

Existing DT and Android dmesg identify:

- `mediatek,mt6768-usb20`, MUSB HDRC, USB 2.0 OTG, 16 endpoints.
- MAC `0x11200000`, PHY register window `0x11cc0000`.
- `mediatek,generic-tphy-v1`, USB2 port `0x11cc0800`.
  The node name still says `11210000`; use `reg`, not the name as an address.
- Interrupt SPI 97 (GIC INTID 129). Linux virtual IRQ numbers are not GIC INTIDs.
- Clocks named `usb0`, `usb0_clk_top_sel`, `usb0_clk_univpll3_d4`;
  PHY reference clock and regulator/role integration also required.
- DT includes Type-C, charger and VBUS components; determine the bound drivers,
  not just enabled/legacy nodes, before selecting board-specific initialization.

Current UEFI includes UsbBusDxe, UsbMassStorageDxe and a fastboot transport but no
MUSB host or device controller implementation. UsbBus consumes a host-controller
protocol; fastboot transport consumes `gUsbDeviceProtocolGuid`. Including these
upper layers does not initialize MAC/PHY or make the device enumerate. There is
also no UsbKbDxe in the active platform component list.

## Needed inputs

Already available: ROM archive, stock boot/LK, base DTB/DTBO, captured runtime FDT
and Android log. Do not request duplicate partition dumps.

1. A fresh `tools/collect_lancelot_usb.py` archive from Android with the cable
   attached: kernel identity/config, runtime FDT, dmesg, bound drivers, role/UDC,
   clock/regulator summaries. Missing debugfs files are acceptable. The script
   does not mount debugfs, change roles, read raw MMIO or write partitions.
2. Preferably the exact source repository + branch/commit for this DerpFest
   kernel. If only source files are available, include the MUSB/MediaTek usb20
   driver, `phy-mtk-tphy.c` or platform PHY source, MT6768 clock driver, USB
   extcon/Type-C and charger/VBUS driver, relevant DTS and config. Android .ko
   files cannot be loaded as UEFI drivers.
3. Choose first target: PC-facing peripheral enumeration/communication, or OTG
   host for a keyboard. A normal cable connected to a PC exercises peripheral
   mode; it cannot verify host mode or VBUS supply to a keyboard.

Windows example (Python installed; adb.exe accessible):

```powershell
py -3 tools\collect_lancelot_usb.py --adb "C:\platform-tools\adb.exe"
```

Alternatively expose the phone to WSL using the user's existing USB forwarding
setup, or run the same script in an environment where `adb devices -l` lists it.
Binary FDT/config data is captured by subprocess, not PowerShell text redirection.

## Implementation sequence

Validate board-specific clock/PHY/power sequence from matching kernel/LK sources;
then add a polled MUSB PIO controller implementation, avoiding DMA/IOMMU assumptions
for the first port. Peripheral EP0 enumeration and descriptors are the first PC
test, followed by bulk transport; host mode separately needs root-port control,
control/bulk/interrupt transactions and Type-C/VBUS integration. Use on-screen
status and bounded timeouts throughout. USB enumeration is not ADB: ADB requires
its own protocol/service. Existing generic fastboot transport also requires an
application to start it, and platform flash support is a separate task.

No new USB-enabled image has been produced at this evidence-collection stage.
