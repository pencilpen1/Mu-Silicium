#!/usr/bin/env python3
"""Read-only checks of the lancelot boot artifacts; no device/logs required.

Run from any directory: python3 tools/audit_lancelot_boot.py
Uses only the standard library. This does not prove that firmware boots.
"""

import gzip
import hashlib
from pathlib import Path
import re
import struct
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def boot_parts(path):
    data = path.read_bytes()
    if data[:8] != b"ANDROID!" or len(data) < 1660:
        raise ValueError(f"{path}: not a supported Android boot image")
    fields = struct.unpack_from("<10I", data, 8)
    ks, ka, rs, ra, ss, sa, ta, page, version, _ = fields
    if version != 2 or page < 1660 or page & (page - 1):
        raise ValueError(f"{path}: expected boot header v2 and valid page size")
    recovery_size = struct.unpack_from("<I", data, 1632)[0]
    header_size, dtb_size, dtb_addr = struct.unpack_from("<IIQ", data, 1644)
    if header_size != 1660:
        raise ValueError(f"{path}: unexpected header size {header_size}")

    def padded(size):
        return (size + page - 1) & ~(page - 1)

    dtb_offset = page + sum(padded(n) for n in (ks, rs, ss, recovery_size))
    if page + ks > len(data) or dtb_offset + dtb_size > len(data):
        raise ValueError(f"{path}: truncated boot image")
    return {
        "data": data,
        "kernel": data[page:page + ks],
        "dtb": data[dtb_offset:dtb_offset + dtb_size],
        "kernel_addr": ka,
        "ramdisk_addr": ra,
        "second_addr": sa,
        "tags_addr": ta,
        "dtb_addr": dtb_addr,
        "dtb_offset": dtb_offset,
        "page": page,
        "version": version,
    }


def fdt_properties(blob):
    """Return path -> property -> (offset, bytes); preserve property alignment."""
    if len(blob) < 40:
        raise ValueError("truncated FDT header")
    magic, total, off_struct, off_strings, _, _, _, _, strings_size, size = (
        struct.unpack_from(">10I", blob)
    )
    if magic != 0xD00DFEED or not 40 <= total <= len(blob):
        raise ValueError("invalid FDT header")
    end = off_struct + size
    strings_end = off_strings + strings_size
    if not 40 <= off_struct < end <= total or strings_end > total:
        raise ValueError("invalid FDT block bounds")
    result = {}
    stack = []
    pos = off_struct
    while pos + 4 <= end:
        token = struct.unpack_from(">I", blob, pos)[0]
        pos += 4
        if token == 1:
            stop = blob.index(b"\0", pos, end)
            stack.append(blob[pos:stop].decode("ascii"))
            pos = (stop + 4) & ~3
        elif token == 2:
            if not stack:
                raise ValueError("unbalanced FDT nodes")
            stack.pop()
        elif token == 3:
            if pos + 8 > end or not stack:
                raise ValueError("invalid FDT property")
            length, name_offset = struct.unpack_from(">II", blob, pos)
            pos += 8
            name_start = off_strings + name_offset
            if pos + length > end or not off_strings <= name_start < strings_end:
                raise ValueError("invalid FDT property bounds")
            stop = blob.index(b"\0", name_start, strings_end)
            name = blob[name_start:stop].decode("ascii")
            path = "/" + "/".join(n for n in stack if n)
            result.setdefault(path, {})[name] = (pos, blob[pos:pos + length])
            pos = (pos + length + 3) & ~3
        elif token == 4:
            continue
        elif token == 9:
            if stack:
                raise ValueError("unclosed FDT nodes")
            return result
        else:
            raise ValueError(f"unknown FDT token {token}")
    raise ValueError("missing FDT end token")


def pcd(path, name):
    match = re.search(r"\." + re.escape(name) + r"\|\s*(0x[0-9a-fA-F]+|\d+)",
                      path.read_text())
    if match is None:
        raise ValueError(f"{path}: missing PCD {name}")
    return int(match[1], 0)


def main():
    image = ROOT / "ws/boot-uefi.img"
    port = boot_parts(image)
    stock = boot_parts(ROOT / "rom-extract/boot.img")
    print(f"Image: {image.relative_to(ROOT)} ({len(port['data'])} bytes)")
    print(f"SHA256: {hashlib.sha256(port['data']).hexdigest()}")
    print(f"Android header v{port['version']}, page={port['page']:#x}")
    for key in ("kernel_addr", "ramdisk_addr", "tags_addr", "dtb_addr"):
        print(f"  {key}: {port[key]:#x}, matches ROM: {port[key] == stock[key]}")
    print(f"  DTB offset: {port['dtb_offset']:#x}")
    print(f"  DTB matches ROM: {port['dtb'] == stock['dtb']}")

    payload = gzip.decompress(port["kernel"])
    shim = (ROOT / "ws/BootShim/BootShim.bin").read_bytes()
    fd = (ROOT / "ws/workspace/Build/MT6768Pkg/DEBUG_GCC5/FV/MT6768PKG_UEFI.fd").read_bytes()
    print(f"Payload: {len(payload):#x}; shim={len(shim):#x}, FD={len(fd):#x}")
    print(f"  Matches current BootShim + FD: {payload == shim + fd}")
    assert payload == shim + fd, "stale/mismatched payload"
    if shim[56:60] != b"ARM\x64":
        raise ValueError("missing ARM64 image magic")
    dest, copy_size = struct.unpack_from("<QQ", shim, 8)
    print(f"  BootShim copy destination={dest:#x}, length={copy_size:#x}")
    print(f"  Copy length matches FD: {copy_size == len(fd)}")
    assert copy_size == len(fd)
    instruction = struct.unpack_from("<I", fd)[0]
    if instruction & 0xFC000000 == 0x14000000:
        immediate = instruction & 0x03FFFFFF
        if immediate & (1 << 25):
            immediate -= 1 << 26
        target = dest + immediate * 4
        print(f"  FD first branch target: {target:#x}")
        fvmap = (ROOT / "ws/workspace/Build/MT6768Pkg/DEBUG_GCC5/FV/FVMAIN_COMPACT.Fv.map").read_text()
        entry = int(re.search(r"EntryPoint=(0x[0-9a-fA-F]+)", fvmap)[1], 16)
        assert target == entry, "FD reset branch does not reach SEC"
        elf = ROOT / "ws/workspace/Build/MT6768Pkg/DEBUG_GCC5/AARCH64/MT6768Pkg/PrePi/PeiUniCore/DEBUG/PrePi.debug"
        symbols = subprocess.check_output(["aarch64-linux-gnu-nm", "-n", str(elf)], text=True)
        def symbol(name):
            return int(re.search(r"^([0-9a-fA-F]+) \w " + name + r"$", symbols, re.M)[1], 16)
        vector = entry - symbol("_ModuleEntryPoint") + symbol("SecVectorTable")
        assert vector % 0x800 == 0, "misaligned runtime SEC VBAR"
        assert struct.pack("<Q", vector) in fd, "missing relocated VBAR literal"
        print(f"  Runtime SEC vector: {vector:#x}, 2-KiB alignment and relocated literal verified")
    print("  NOTE: Linux text_offset field is reused as an absolute copy destination.")

    props = fdt_properties(port["dtb"])
    pkg = ROOT / "ws/MT6768Pkg"
    device = pkg / "Devices/lancelot.dsc"
    framebuffer = pcd(device, "PcdMipiFrameBufferAddress")
    print("FDT (on disk, NOT LK's runtime-patched FDT):")
    if props["/"]["#address-cells"][1] != struct.pack(">I", 2) or (
        props["/"]["#size-cells"][1] != struct.pack(">I", 2)
    ):
        raise ValueError("this audit expects 2 address and 2 size cells")
    for path, values in props.items():
        if re.fullmatch(r"/memory(?:@[^/]+)?", path) and "reg" in values:
            offset, data = values["reg"]
            print(f"  {path}/reg offset={offset:#x}, offset % 8={offset % 8}")
            for base, size in struct.iter_unpack(">QQ", data):
                print(f"    DDR [{base:#x}, {base + size:#x})")
                if framebuffer >= base + size:
                    print("    RISK: fixed FB is at/above DDR end; the DDR-only map walk omits Display.")
            if offset % 8:
                print("    NOTE: requires alignment-safe FDT reads (current parser uses byte reads).")
        if path.startswith("/reserved-memory/"):
            for name in ("alloc-ranges", "alloc-range"):
                if name not in values:
                    continue
                for base, size in struct.iter_unpack(">QQ", values[name][1]):
                    print(f"  {path}/{name}: [{base:#x}, {base + size:#x})")
                    if base <= dest < base + size:
                        print("    RISK: UEFI destination lies in this allocation window.")
            if "reg" not in values:
                print(f"  {path}: no fixed reg; this is NOT a known reservation address.")

    print("Timer INTIDs (DT PPI number + 16; not raw DT PPI numbers):")
    interrupts = list(struct.iter_unpack(">III", props["/timer"]["interrupts"][1]))
    for label, name, irq in zip(
        ("secure", "physical", "virtual", "hypervisor"),
        ("PcdArmArchTimerSecIntrNum", "PcdArmArchTimerIntrNum",
         "PcdArmArchTimerVirtIntrNum", "PcdArmArchTimerHypIntrNum"),
        interrupts,
    ):
        kind, number, _ = irq
        expected = number + (16 if kind == 1 else 32)
        config = pkg / "MT6768Pkg.dsc"
        try:
            actual = pcd(config, name)
        except ValueError:
            actual = pcd(ROOT / "ws/edk2/ArmPkg/ArmPkg.dec", name)
        print(f"  {label}: expected={expected}, configured={actual}, match={actual == expected}")

    stack = pcd(device, "PcdPrePiStackBase")
    stack_size = pcd(device, "PcdPrePiStackSize")
    pool = pcd(device, "PcdUefiMemPoolBase")
    pool_size = pcd(device, "PcdUefiMemPoolSize")
    print(f"Actual SEC stack: [{stack:#x}, {stack + stack_size:#x})")
    print(f"PrePi stack PCD: [{stack:#x}, {stack + stack_size:#x})")
    runtime = (ROOT / "analysis/runtime-2026-10-17/lancelot-runtime.dtb").read_bytes()
    runtime_props = fdt_properties(runtime)
    reservations = []
    offset = struct.unpack_from(">I", runtime, 16)[0]
    while True:
        base, size = struct.unpack_from(">QQ", runtime, offset)
        offset += 16
        if base == size == 0:
            break
        reservations.append((base, size))
    for path, values in runtime_props.items():
        if path.startswith("/reserved-memory/") and "reg" in values:
            reservations.extend(struct.iter_unpack(">QQ", values["reg"][1]))
    vectors = pcd(device, "PcdCpuVectorBaseAddress")
    log_config = (pkg / "Include/Configuration/BootLogConfig.h").read_text()
    log_base = int(re.search(r"#define LANCELOT_LOG_BASE\s+(0x[0-9a-fA-F]+)", log_config)[1], 16)
    log_size = int(re.search(r"#define LANCELOT_LOG_SIZE\s+(0x[0-9a-fA-F]+)", log_config)[1], 16)
    regions = [("FD", dest, copy_size), ("pool", pool, pool_size),
               ("stack", stack, stack_size), ("exception stack", vectors, 0x10000),
               ("boot log", log_base, log_size)]
    for label, base, size in regions:
        assert base % 4096 == size % 4096 == 0, label
        assert all(not (base < a+s and a < base+size)
                   for other, a, s in regions if other != label), label
        assert all(not (base < a+s and a < base+size) for a, s in reservations if s), label
        print(f"  {label} [{base:#x}, {base+size:#x}): no overlap with captured runtime reservations")
    print("Offline checks only: entry EL, live reservations and display scanout remain unknown.")


if __name__ == "__main__":
    main()
