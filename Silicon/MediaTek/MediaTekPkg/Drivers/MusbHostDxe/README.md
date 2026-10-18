# lancelot MUSB Host 0.1

First UEFI Host bring-up for Redmi 9 / MT6768. Build and scripted-MMIO tests
pass; phone validation of this driver is pending. Android has independently
enumerated the Lenovo SX1 64G (`17ef:38ac`) at high speed on this board.

## Build and test

From the repository root:

```sh
python3 Resources/Scripts/test_lancelot_usb_host.py
python3 build_uefi.py -d lancelot -r RELEASE
python3 Resources/Scripts/audit_lancelot_usb_host.py
```

`Platforms/Xiaomi/lancelotPkg/lancelot.dsc` selects `LANCELOT_USB_HOST=1` by
default. Setting the define to `0` selects the existing Device profile. FDF
selection includes exactly one MAC owner: `MusbHostDxe` or `MusbDiagnosticDxe`.
The public DSC may still compile the Device module; it is absent from the Host
firmware volume. Boot image addresses and packaging follow the existing platform.

The Host profile includes `UsbBusDxe`, `UsbKbDxe` and `UsbMassStorageDxe` in the
FV. Its device boot-manager hook adds the USB boot-keyboard class path to ConIn,
connects controllers and refreshes boot options. BootLog holds Host diagnostics.
The Host image does not offer PC fastboot.

## Bottom status text

Two English text lines at the bottom refresh once per second from cached driver
state. They show waiting for OTG, checking VBUS, powered/waiting for a device,
resetting, enumerating, and configured keyboard/storage/HID/hub plus VID:PID,
address and speed. A keyboard report changes the label to `INPUT RECEIVED`.
`CONFIGURED` means successful USB SET_CONFIGURATION, not a filesystem mount or
proof of working keyboard input. Descriptor walking is bounds checked.

Failures show a short phase and EFI status (I2C/Type-C, VBUS, PHY, endpoint STALL,
transfer timeout or unsupported hub split transfer). The repaint is a separate
TPL_CALLBACK event, not part of the packet polling loop. A board-init failure
keeps a status-only resident module without publishing a Host Controller protocol;
its display never touches an uninitialized MAC. The existing 5x12 font supports
ASCII, so these status messages are English rather than Chinese.

## Implementation

- One root port, `EFI_USB2_HC_PROTOCOL`, PIO for low/full/high-speed devices.
- EP0 control with SETUP, bounded data/status stages and short-packet handling.
- Local EP1 synchronous bulk/interrupt, up to 512-byte FIFO packets. Remote
  endpoint number/direction and device address are programmed per transfer.
- Four asynchronous interrupt-IN pipes on local EP2..5, one packet per request,
  at most 64 bytes, 1..255 ms polling interval. NAK preserves the pipe; callbacks
  can cancel themselves. No busy wait in the asynchronous poller.
- MTK targets use the `0x480` bank; toggles use the 32-bit RXTOG/TXTOG write-enable
  mechanism. Standard Mentor target offsets would collide with these registers.
- All indexed MMIO is serialized at TPL_NOTIFY. Client callbacks execute at
  TPL_CALLBACK outside that critical section. Synchronous timeouts use one budget
  across the whole transfer; timeout `0` uses a bounded 5-second bring-up default.
- Root connect/disconnect/reset/suspend/resume and change bits; 50 ms reset,
  20 ms recovery. Errors abort FIFO requests and preserve actual transfer length.
- No DMA, isochronous, high-bandwidth interrupt or split transactions behind
  high-speed hubs. Unsupported TT routes return EFI_UNSUPPORTED. Use a direct
  OTG keyboard/flash drive for the first test; hub topologies need further validation.

## Board support

`LancelotUsb.c` uses the existing clock protocol and a local I2C7-v2 PIO backend.
The UEFI map adds I2C7 `0x11004000` and the page containing its DMA channel
`0x11000680`. GPIO175/176 mode 1 is SCL7/SDA7; masked changes preserve neighboring
pin muxes. I2C uses open drain, divide-by-5 and computed 100 kHz timing with the
MT6768 counter encoding constraint. Reads are combined write/repeated-start/read.
NACK, FIFO-count mismatch and a 5 ms timeout return errors and reset that controller.

WUSB3801 at I2C `0x60` is identified before use, set to DRP/default USB current
advertisement with the interrupt pin masked, and its status is polled every
50 ms. Only two consecutive sink-partner observations permit enabling VBUS.
An attached PC/source, detach or I2C failure keeps/stops Host power. SMB1351 at
`0x55` uses a read-modify-write of command register `0x32` bit 0 with readback.
The LK current-limit configuration is preserved: the reference Linux
`smb1351_set_otg_current(500000)` path has an uninitialized encoding and was not
ported. Failed power-off writes are retried before accepting another attach.

The existing working USB PLL/rails are inherited. After VBUS rise, MUSB SESSION
is cleared, tphy is set invalid, then SESSION and Host PHY are enabled after
5 ms. ExitBootServices stops transactions, disables VBUS and restores the saved
Type-C control byte. Full PD negotiation and OS handoff power policy are not
implemented in this initial driver.

## Evidence and regression scope

Reference: user-provided `MrShockWAVEog/shockwave_kernel_xiaomi_mt6768`, branch
`17.0`, commit `461f0c72b8ee70f4973dc2becd98c5801b0e31e0`:

- `drivers/misc/mediatek/usb20/{musb_host.c,musb_virthub.c,mtk_musb_reg.h}`
- `drivers/misc/mediatek/usb20/mt6768/{usb20.c,usb20_host.c}`
- `drivers/phy/mediatek/phy-mtk-tphy.c`
- `drivers/i2c/busses/{i2c-mtk.c,i2c-mtk.h}`
- `drivers/pinctrl/mediatek/pinctrl-mtk-mt6768.h`
- `drivers/misc/mediatek/typec/tcpc/{tcpc_wusb3801.c,inc/wusb3801.h}`
- `drivers/power/supply/mediatek/charger/smb1351-charger.c`
- `drivers/misc/mediatek/extcon/extcon-mtk-usb.c`

[Android captures and porting notes](../../../../../docs/lancelot_usb_host_bringup.md)
record the physical role transitions and the source-version evidence boundary.

The tests compile the actual Host and board C files against Mu headers with
ASan/UBSan. Scripted packets cover control IN/OUT/status, bulk endpoint routing,
short transfers, NAK retry, STALL, timeout with partial progress, hardware toggle,
babble bounds, asynchronous self-cancel/port-disable, root reset/disconnect,
Type-C VBUS gating, power-off retry and EBS. Separate I2C tests check the v2
register layout, repeated starts, bounded failures, preserved mux/current-limit
fields and masked charger writes. They are regression tests, not a hardware model.
