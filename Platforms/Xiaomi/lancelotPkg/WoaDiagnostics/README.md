# Lancelot Windows PE bring-up

This experimental profile targets the 4 GiB Redmi 9 (MT6769V / MT6768, eMMC).
It prepares the firmware handoff for ARM64 Windows PE and a Windows USB logging
driver. The default ACPI profile enables CPU0 only. It has been compiled and
validated offline. The 26100 PE reached the kernel and user mode on the target;
the initial run had a blank screen after the boot logo, so visible desktop
startup is still under investigation.

The firmware reads PE from eMMC into RAM. A native Windows MSDC storage driver
is still required for a full installation on eMMC. GPIO, touch, USB host, power
management and GPU support are separate work.

## Platform changes

- Describe all 4 GiB from physical `0x40000000` through `0x140000000`, retaining
  fixed DT reservations and the conservative gaps captured from Android iomem.
- Use GICv3, MPIDRs `0x000, 0x100, ..., 0x700`, correct timer polarity, hardware
  reduced ACPI and PSCI through SMC. Do not expose unsupported SDHCI/16550 layouts.
- Keep eMMC TOP at `0x11cd0000` and SD TOP at `0x11c90000`. Enumerate eMMC only
  during initial bring-up and bound reset, FIFO, clock, busy and OCR waits.
- Decode MSDC TXCNT from bits 23:16 before computing FIFO free space. Bound
  command and PIO data waits with the generic counter, prefer hardware errors
  over simultaneous completion, and signal successful PassThru event requests.
- Select the active OVL surface whose address, pitch and geometry match the
  boot framebuffer. Disable per-pixel alpha and select constant blending for
  that surface, including physical and extended layers. Record the register
  state; never change an unrelated overlay.
- Start the MUSB device log transport before storage and preserve the SEC ring.
  This profile uses the USB cable for diagnostics, rather than USB host devices.
- Publish LDBG and a 128-byte `lancelot_handoff.h` contract in ACPI NVS at
  `0x512f0000`. The firmware ring remains at `0x512c0000`, length `0x30000`.
- Disable the generic kernel errata patch hook for lancelot. Boot with Secure
  Boot disabled for this test profile.

The ACPI source generator, hardware profile, generated AML and checksums are in
this directory. The upstream Silicium-ACPI gitlink does not change. `Acpi/up` is
the initial profile. `Acpi/a55x2` and `Acpi/a55x6` enable two and six A55 cores;
`Acpi/smp` adds both A75 cores. MADT Enabled and DSDT `_STA` use the same CPU
set. These multicore profiles require Windows target validation.

DSDT exposes `ACPI\\LNC0001`, display class 03/00/00, the fixed framebuffer
reservation at `0x7cdb0000` and OVL registers at `0x1400b000`. A board-specific
Windows display-only miniport must validate these resources and the LDBG
framebuffer handoff before drawing. GOP format 0 is RGBX; Windows presents
BGRX and needs color conversion.

## Build and boot media

Use the repository's usual Mu setup, dependencies and CLANGPDB toolchain first.
The existing patches under `Resources/MuPatches` are part of that setup; they
include the timer fallback, ARM assembler compatibility and runtime variable
guard. No new dependency repository commit is needed for this profile.

From the repository root:

```sh
python3 Platforms/Xiaomi/lancelotPkg/WoaDiagnostics/BuildWoa.py --profile up
```

Outputs are under `Build/WoaArtifacts`: a 2 MiB FD, its Android boot-v2/gzip
image, `LancelotPreflight.efi`, a build log and SHA256 manifest. The Android
header uses 2048-byte pages, FD load address `0x40080000`, and no BootShim.
`--profile smp` selects the prepared eight-core tables and is not the first test
profile. The script builds files locally and never accesses or flashes a phone.

Place `LancelotPreflight.efi` at ESP `\EFI\BOOT\BOOTAA64.EFI` and optionally
`\EFI\WOA\LancelotPreflight.efi`. The matching Microsoft ARM64 boot manager must
be at `\EFI\Microsoft\Boot\bootmgfw.efi`, with its BCD, boot.sdi and PE WIM.
Preflight saves the actual ACPI tables, memory map, GOP state, SecureBoot state,
exception level and counter frequency to ESP `\WOA\logs\` before chainloading.
It also queries PSCI_VERSION, CPU_ON64 support and each core's AFFINITY_INFO.
These queries do not start or stop cores; Windows starts selected cores via
the PSCI SMC conduit described in FADT.

BOOT must be flashed with the phone's native fastboot. Avoid buffered BOOT
writes from Android: the supplied 4.19 kernel stalled in `wbt_wait` during such
a write. Keep a verified original Android BOOT image and partition backup.

## Diagnostics and limits

Firmware uses LLOG1 over a diagnostic fastboot endpoint, serial `lancelot-uefi`.
A Windows driver implementing the LDBG contract can take ownership after
ExitBootServices and re-enumerate KLOG2. The endpoint is not a COM port or a
general flashing/reboot service. UART metadata describes UART0 at `0x11002000`,
GSI 144, 115200 baud, with 32-bit accesses and stride four.

There is a real observation gap between ExitBootServices and Windows driver
entry. Firmware callbacks cannot keep USB alive after the OS takes ownership.
Physical UART/KD may be required to diagnose a failure in that interval.

The Windows image must independently enable test signing, disable HVCI/VBS for
this prototype, and contain the signed ARM64 diagnostic consumer. The firmware
does not change the host PC's policy or supply native Windows eMMC support.

Offline checks cover ACPI checksums and record boundaries, CPU topology and
interrupt resources, reserved-memory coverage, BOOT header/FD identity and
partition fit. Host tests exercise the actual MUSB protocol, log ring and
handoff validation, plus bounded MSDC waits.

The first captured target boot reached eMMC, FAT and `LancelotPreflight.efi`,
then stopped while flushing a newly created diagnostics directory. The saved
ESP had damaged directory entries; the PE WIM and preflight still matched their
original hashes. The old TXCNT expression left the field shifted and caused
the FIFO free-space subtraction to wrap. The fixes above address that defect
and the missing timeout/completion handling. The next run reached
ExitBootServices, the Windows driver, PE user mode and automatic diagnostics.
The ESP filesystem remained valid. Visible desktop output is a separate
validation requirement.

With Clang available on a Linux host, test the actual firmware functions:

```sh
python3 Platforms/Xiaomi/lancelotPkg/WoaDiagnostics/Tests/test_msdc_transfers.py --mu . --prove-regression
```

The stateful FIFO model checks data integrity, FIFO capacity, partial reads,
missing completion, CRC priority and callback lifetime under ASan/UBSan.
`--prove-regression` also verifies that restoring the old TXCNT expression
makes the test fail.

The display selection test also uses the production SEC functions and checks
that only a matching active framebuffer surface can authorize MMIO writes:

```sh
python3 Platforms/Xiaomi/lancelotPkg/WoaDiagnostics/Tests/test_boot_display.py --mu .
```
