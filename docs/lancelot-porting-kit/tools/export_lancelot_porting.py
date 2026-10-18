#!/usr/bin/env python3
"""Export lancelot UEFI sources, hardware evidence and a machine-readable index.

Run from any directory. Uses only Python's standard library; never modifies the
firmware tree. Output must be a new file. Upstream checkout revisions are in
docs/dependencies.md; this is a porting kit, not a standalone EDK2 distribution.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import tarfile

from audit_lancelot_boot import fdt_properties


ROOT = Path(__file__).resolve().parents[1]
PREFIX = "lancelot-porting-kit"
RUNTIME = "analysis/usb-2026-10-17-root/runtime.dtb"


def region(base, size):
    return {"base": hex(base), "size": hex(size), "end_exclusive": hex(base + size)}


def hardware_index():
    blob = (ROOT / RUNTIME).read_bytes()
    nodes = fdt_properties(blob)
    if any(nodes["/"][name][1] != struct.pack(">I", 2)
           for name in ("#address-cells", "#size-cells")):
        raise ValueError("Expected captured root FDT with 2/2 address/size cells")
    reserved = []
    for path, props in nodes.items():
        if path.startswith("/reserved-memory/") and "reg" in props:
            if len(props["reg"][1]) % 16:
                raise ValueError(f"Unsupported reservation tuple at {path}")
            for base, size in struct.iter_unpack(">QQ", props["reg"][1]):
                reserved.append({"node": path, **region(base, size), "no_map": "no-map" in props})
    reservation_map = []
    offset = struct.unpack_from(">I", blob, 16)[0]
    while True:
        base, size = struct.unpack_from(">QQ", blob, offset)
        offset += 16
        if base == size == 0:
            break
        reservation_map.append(region(base, size))
    memory = []
    for path, props in nodes.items():
        if re.fullmatch(r"/memory(?:@[^/]+)?", path) and "reg" in props:
            memory.extend(region(base, size) for base, size in struct.iter_unpack(">QQ", props["reg"][1]))

    # Preserve every property byte, including phandles, interrupt parents, cell
    # counts and pinctrl/clock references. Raw cells are not MMIO register values.
    raw_nodes = {
        path: {name: {"hex": value.hex()} for name, (_, value) in props.items()}
        for path, props in nodes.items()
    }
    for props in raw_nodes.values():
        for name in ("compatible", "status", "clock-names", "phy-names", "method", "dr_mode", "model"):
            if name in props:
                value = bytes.fromhex(props[name]["hex"])
                props[name]["strings"] = value.rstrip(b"\0").decode("utf-8", errors="replace").split("\0")
    dsc = ROOT / "ws/MT6768Pkg/Devices/lancelot.dsc"
    pcds = dict(re.findall(r"^\s*(g\w+\.Pcd\w+)\|\s*(0x[0-9a-fA-F]+|\d+)\s*$", dsc.read_text(), re.M))
    config = (ROOT / "ws/MT6768Pkg/Include/Configuration/BootLogConfig.h").read_text()
    log_base = int(re.search(r"#define LANCELOT_LOG_BASE\s+(0x[0-9a-fA-F]+)", config)[1], 16)
    log_size = int(re.search(r"#define LANCELOT_LOG_SIZE\s+(0x[0-9a-fA-F]+)", config)[1], 16)
    return {
        "schema_version": 1,
        "board": "Redmi 9 / lancelot / MT6768 / tested 4 GB + 64 GB",
        "evidence": "Captured Android runtime FDT; not a live UEFI handoff or proof that all devices have UEFI drivers",
        "source": {"path": RUNTIME, "sha256": hashlib.sha256(blob).hexdigest()},
        "address_encoding": "Addresses/sizes are hexadecimal strings; intervals are [base,end_exclusive)",
        "dt_property_encoding": "hex is the original property bytes (big-endian cells); empty hex represents a boolean property",
        "ddr_banks": memory,
        "reservation_map": reservation_map,
        "fixed_reserved_memory": sorted(reserved, key=lambda r: int(r["base"], 16)),
        "uefi_device_pcds": {"source": str(dsc.relative_to(ROOT)), "values": pcds},
        "uefi_boot_log": {"source": "ws/MT6768Pkg/Include/Configuration/BootLogConfig.h", **region(log_base, log_size)},
        "dt_nodes": raw_nodes,
    }


def source_files():
    paths = {"README.md", "ws/.gitignore", "ws/BootShim/BootShim.S", "ws/BootShim/Makefile",
             "ws/build.sh", "ws/build_common.sh", "ws/build_bootshim.sh", "ws/firstrun.sh",
             "ws/mkbootimg.py", "ws/blobs/dtb", "ws/blobs/initramfs",
             "analysis/runtime-2026-10-17/lancelot-runtime.dtb",
             "analysis/runtime-2026-10-17/fb-info.txt", RUNTIME}
    for folder in ("ws/MT6768Pkg", "docs", "patches"):
        paths.update(str(p.relative_to(ROOT)) for p in (ROOT / folder).rglob("*") if p.is_file())
    paths.update(str(p.relative_to(ROOT)) for p in (ROOT / "tools").iterdir()
                 if p.is_file() and p.suffix in (".py", ".c", ".sh"))
    for name in ("usb-bindings.txt", "power-bindings.txt", "phy-inventory.txt",
                 "usb-role-state.txt", "kernel.config"):
        paths.add("analysis/usb-2026-10-17-root/" + name)
    return sorted(paths)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("/tmp/opencode/lancelot-porting-kit.tar.gz"))
    args = parser.parse_args()
    # Read the complete selected snapshot before writing an archive.
    contents = {name: (ROOT / name).read_bytes() for name in source_files()}
    contents["hardware.json"] = (json.dumps(hardware_index(), ensure_ascii=False, indent=2) + "\n").encode()
    contents["KIT.md"] = """# lancelot UEFI porting kit

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
""".encode()
    contents["MANIFEST.sha256"] = "".join(
        f"{hashlib.sha256(data).hexdigest()}  {name}\n" for name, data in sorted(contents.items())
    ).encode()
    with args.output.open("xb") as output, tarfile.open(fileobj=output, mode="w:gz") as archive:
        for name, data in sorted(contents.items()):
            info = tarfile.TarInfo(f"{PREFIX}/{name}")
            info.size = len(data)
            info.mode = 0o755 if name.endswith(".sh") else 0o644
            archive.addfile(info, io.BytesIO(data))
    print(f"Exported {len(contents)} files: {args.output} ({args.output.stat().st_size} bytes)")
    print("Entry: lancelot-porting-kit/KIT.md; parameters: hardware.json")


if __name__ == "__main__":
    main()
