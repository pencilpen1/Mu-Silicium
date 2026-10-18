# lancelot MUSB dual-role / Host 0.3

UEFI USB bring-up for Redmi 9 / MT6768. Current status (2026-10-18, implementation
baseline `86adf67b`, image `v0.3-inputfix`):

- **Hardware-confirmed by the user:** USB keyboard controls UEFI; Host/Device
  switching recovered on v0.3; fastboot device discovery works. The keyboard
  was previously enumerated as `0c45:760b` at low speed.
- **Still pending:** UEFI USB-drive enumeration/access, current Mu getvar/log
  hardware retest, keyboard LED retest and broader hotplug/suspend coverage.
- **Latest fixes:** GPIO93 phantom SCAN_DOWN correction. The temporary bottom
  USB status/test-character overlay has been removed. Builds and regression tests pass.
- **Not implemented:** split transactions behind high-speed hubs, isochronous
  transfers and full PD negotiation.

The 0.2 dual-role regression is historical; it must not replace the user's
later successful switching/keyboard feedback. Android's Lenovo SX1 64G
(`17ef:38ac`) high-speed enumeration is a reference, not UEFI storage validation.
See the [current USB status table](../../../../../docs/lancelot_usb_host_bringup.md#current-usb-status).

## Build and test

From the repository root:

```sh
python3 Resources/Scripts/test_lancelot_usb_host.py
python3 Resources/Scripts/test_lancelot_musb.py
python3 build_uefi.py -d lancelot -r RELEASE
python3 Resources/Scripts/audit_lancelot_usb_host.py
```

`Platforms/Xiaomi/lancelotPkg/lancelot.dsc` selects `LANCELOT_USB_HOST=2` by
default: automatic dual role. `0` selects the original standalone Device profile;
`1` selects Host only. The dual FV contains the Host/role manager and the managed
Device module, while the standalone Device module is excluded. Boot image
addresses and packaging follow the existing platform.

The Host profile includes `UsbBusDxe`, `UsbKbDxe` and `UsbMassStorageDxe` in the
FV. Its device boot-manager hook adds the USB boot-keyboard class path to ConIn,
connects controllers and refreshes boot options. BootLog holds Host diagnostics.
In the default dual-role image, a PC/source partner selects the original
full-speed fastboot Device transport; an OTG/sink partner selects Host.

## Exclusive ownership and Device compatibility

`MtkUsbRole.h` registers the resident Device driver's start/stop callbacks.
The managed Device INF waits for that protocol instead of starting hardware on
its own. At TPL_NOTIFY the manager stops the old owner, verifies Host VBUS is off
before enabling Device, and reinitializes Host FIFO/PHY state before returning
to Host. Host polling, transfers and old root-hub/cancel/reset calls cannot touch
MAC registers while Device owns them. Both EBS stop paths are idempotent.

Device EP0/bulk descriptors and packet state machines are unchanged. Its timer
is canceled on handoff and can restart on the next PC connection. In managed
mode it attaches to the shared boot log rather than clearing earlier Host logs.
When Host board initialization fails, the manager can still start the original
passive Device path, without sourcing VBUS or exposing an unavailable Host HC.
A transient Type-C read error does not stop an already active Device transport.

## Screen output

The temporary bottom USB status/test-character overlay has been removed. The Host
driver no longer allocates a framebuffer surface, paints the bottom rows, or runs
a display timer. USB role management, Host/Device transport, boot logging and
keyboard input continue to run without screen diagnostics; failures remain
available through the boot log and protocol results.

## GPIO93 phantom down-key correction

The user reports that the keyboard controls UEFI but SCAN_DOWN repeats even with
the keyboard disconnected. The lancelot keypad maps active-low volume-down to
GPIO93. MediaTek DIR=0 is input, while the old `GpioGetState()` selected the output
latch in that case. An idle high input with a low output latch therefore looked
like a permanently pressed key. The input/output register selection is corrected;
the existing key press/repeat/release policy is retained. A regression compiling
the actual GPIO and lancelot keypad C reproduced the idle phantom presses before
the fix and passes afterwards. Hardware confirmation remains pending.

References: the pinned kernel's `pinctrl-paris.c` DIR semantics and lancelot DTS
GPIO93/KPCOL0 pull-up/input configuration; the UEFI MT6768 GPIO layout uses DIN
offset `0x200` and DOUT `0x100`. No USB key or direction scan code is suppressed.

## Hotplug corrections in 0.2

- Process coalesced DISCONNECT and CONNECT instead of losing the new connect.
- Reconcile missing edges with stable hardware samples: OPSTATE `0x21` signals
  waiting/disconnect as in the MT6768 reference. Before enumeration, Host-mode
  LSDEV/FSDEV can recover a lost connect. LSDEV/FSDEV absence alone never removes
  a high-speed device. Debounce is 20 ms and reset recovery is guarded.
- Clear stale MAC events before VBUS rise, and re-arm an ended session while an
  OTG adapter remains present (at most once a second, no charger power cycling).
- Abort old endpoint traffic but keep request records until their owner cancels.
- Re-arm an asynchronous IN that became idle without a report, preserving WZC
  receive bits and hardware toggle rather than inventing a successful packet.

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
50 ms using interrupt read-to-clear followed by status. Role changes settle for
50 ms before activation. Only a confirmed sink partner permits enabling VBUS.
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
The new tests cover missed/coalesced port events, reset chatter, ended sessions,
idle-IN rearm, HID/LED observations and Host -> Device -> Host ownership. The
Device tests compile both variants and exercise the actual managed entry and
start/stop callbacks, including getvar after reactivation and preservation of logs.

## No-OS standby

The lancelot USB BDS hook now stays in an application-level event wait when no
bootable OS is found. Previously it returned to the shared policy, which called
shutdown after ten seconds. That path can end USB services while the last
framebuffer text remains visible; its presence is confirmed in code, but it has
not been proven to explain the reported 0.2 failure. F1 opens the existing Boot
Manager and F2 rescans. Other console keys are drained silently so repeats do
not flood the screen; normal menu input remains available. A 100 ms
timer wait (or a bounded stall if timer creation fails) keeps normal callbacks
serviceable. The tests exercise this path beyond ten seconds and verify menu,
rescan and key processing without returning to the shutdown policy.
