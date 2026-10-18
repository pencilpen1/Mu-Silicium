# lancelot UEFI porting kit

Start with `docs/lancelot_uefi_porting_guide.md` (Chinese parameter/driver guide).
`hardware.json` contains the captured Android FDT properties, all fixed reserved
ranges, reservation-map entries and current device PCDs. Preserve its evidence
boundaries: hardware presence does not imply a working UEFI driver.

Sources are under `ws/MT6768Pkg/` and `ws/BootShim/`; retain per-file licenses
(MUSB is GPL-2.0-only). `docs/dependencies.md` records external EDK2 versions and
how to apply `patches/edk2-lancelot-dxe-probes.patch` to a clean matching checkout.
Upstream checkout directories, ROM partitions and built images are separate.
`tools/fork_pkg.sh` is historical, not a build/setup entry point.

Verify the extracted files on Linux: `sha256sum -c MANIFEST.sha256`.
Tests require the dependencies described above. The image audit additionally
requires the original ROM boot image and fresh build artifacts.

USB v0.2 enumeration/getvar was hardware-tested. Current v0.3 boot-log changes
have host/build validation only. PC capture runs in WSL/Linux; see
`docs/lancelot_usb_logging.md` and `tools/read_lancelot_log.py`.
