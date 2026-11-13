#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Build the lancelot WOA profile from this repository and package BOOT + preflight.

Run after the normal Mu dependency/toolchain setup. This command never flashes
a phone. BOOT deployment must use native fastboot.
"""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys


def describe(path):
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--workspace", type=Path)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--profile", choices=("up", "a55x2", "a55x6", "smp"), default="up")
    args = ap.parse_args()
    diag = Path(__file__).resolve().parent
    root = (args.workspace or diag.parents[3]).resolve()
    diag = root / "Platforms/Xiaomi/lancelotPkg/WoaDiagnostics"
    output = (args.output or root / "Build/WoaArtifacts").resolve()
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(diag / "Tools/generate_acpi.py"),
                    "--hardware", str(diag / "hardware-lancelot-4g.json"),
                    "--out", str(diag / "Acpi")], check=True)
    inf = diag / "AcpiTables.inf"
    original_inf = inf.read_bytes()
    text = original_inf.decode()
    table_names = ("APIC", "DSDT", "FACP", "GTDT", "DBG2", "SPCR", "LDBG")
    acpi = {name: describe(diag / "Acpi" / args.profile / (name + ".aml")) for name in table_names}
    log = output / "build.log"
    try:
        # Profile selection is generated build input, derived from this checked
        # script and committed AML. Restore the tracked INF even on failure so
        # all profiles retain the same reproducible publication source set.
        inf.write_text(text[:text.index("[Binaries]")] + "[Binaries]\n" + "".join(
            f"  ASL|Acpi/{args.profile}/{name}.aml\n" for name in table_names))
        with log.open("w") as stream:
            result = subprocess.run([sys.executable, "Platforms/Xiaomi/lancelotPkg/DeviceBuild.py",
                                     "TARGET=DEBUG", "ENABLE_SECUREBOOT=0", "FD_BASE=0x40080000",
                                     "FD_SIZE=0x200000", "FD_BLOCKS=0x200", "DEVICE_MODEL=0"],
                                    cwd=root, stdout=stream, stderr=subprocess.STDOUT)
    finally:
        inf.write_bytes(original_inf)
    if result.returncode:
        print("\n".join(log.read_text(errors="replace").splitlines()[-60:]))
        raise SystemExit(result.returncode)
    build = root / "Build/lancelotPkg/DEBUG_CLANGPDB"
    fd = (build / "FV/SILICIUM_UEFI.fd").read_bytes()
    if len(fd) != 0x200000:
        raise ValueError("Unexpected FD size")
    stem = "Mu-lancelot-woa-" + args.profile
    (output / (stem + ".fd")).write_bytes(fd)
    kernel = output / (stem + ".gz")
    kernel.write_bytes(gzip.compress(fd, mtime=0))
    image = output / (stem + ".img")
    subprocess.run([sys.executable, str(root / "Resources/Scripts/mkbootimg.py"),
                    "--kernel", str(kernel), "--ramdisk", str(root / "Resources/ramdisk"),
                    "--base", "0x40078000", "--kernel_offset", "0x8000",
                    "--ramdisk_offset", "0x07c08000", "--tags_offset", "0x0bc08000",
                    "--dtb_offset", "0x0bc08000", "--dtb", str(root / "Resources/DTBs/lancelot.dtb"),
                    "--header_version", "2", "--cmdline", "bootopt=64S3,32N2,64N2", "-o", str(image)], check=True)
    raw = image.read_bytes()
    size, address = struct.unpack_from("<II", raw, 8)
    page, version = struct.unpack_from("<II", raw, 36)
    if (raw[:8], address, page, version) != (b"ANDROID!", 0x40080000, 2048, 2):
        raise ValueError("Unexpected Android BOOT header")
    if gzip.decompress(raw[page:page + size]) != fd or len(raw) >= 64 * 1024 * 1024:
        raise ValueError("BOOT payload or partition fit verification failed")
    shutil.copyfile(build / "AARCH64/LancelotPreflight.efi", output / "LancelotPreflight.efi")
    symbols = output / "symbols"
    symbols.mkdir(exist_ok=True)
    for name in ("LancelotPreflight.pdb", "LancelotDiagDxe.pdb", "Sec.pdb", "DxeCore.pdb", "MsdcDxe.pdb"):
        matches = list(build.rglob(name))
        if matches:
            shutil.copyfile(matches[0], symbols / name)
    commit = subprocess.run(["git", "-C", str(root), "rev-parse", "HEAD"],
                            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
    manifest = {"profile": args.profile, "source_commit": commit.stdout.strip() if commit.returncode == 0 else None,
                "secure_boot": False, "boot_method": "native fastboot only",
                "hardware_boot_verified": False, "selected_acpi": acpi, "files": {
                    p.name: describe(p) for p in (image, output / (stem + ".fd"), output / "LancelotPreflight.efi")}}
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps(manifest, indent=2))


if __name__ == "__main__":
    main()
