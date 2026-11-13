#!/usr/bin/env python3
"""Generate MT6768 bring-up ACPI from the captured hardware manifest.

Data tables are packed from the ACPI 6.5 layout, then independently inspected
with ACPICA. CPU AML is compiled from generated ASL with iasl.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def table(signature: str, revision: int, body: bytes) -> bytes:
    data = bytearray(struct.pack("<4sIBB6s8sI4sI", signature.encode(), 36 + len(body),
                                revision, 0, b"LNCLOT", b"WOA6768 ", 1, b"WOAB", 1) + body)
    data[9] = -sum(data) & 255
    return bytes(data)


def gicc(uid: int, mpidr: int, enabled: bool, big: bool) -> bytes:
    c = bytearray(82)  # ACPI 6.5 GICC; SPE and TRBE are explicitly zero.
    struct.pack_into("<BBHIIIIIQQQQIQQBBH", c, 0, 11, 82, 0, uid, uid,
                     int(enabled), 0, 23, 0, 0, 0, 0, 25, 0, mpidr, int(big), 0, 0)
    return bytes(c)


def make_madt(hw: dict, smp: bool) -> bytes:
    body = bytearray(8)
    for uid, cpu in enumerate(hw["cpus"]):
        body += gicc(uid, cpu["mpidr"], smp or uid == 0, cpu["compatible"] == "arm,cortex-a75")
    gic = hw["devices"]["gic"]["reg"]
    body += struct.pack("<BBHIQIB3x", 12, 24, 0, 0, gic[0][0], 0, 3)
    body += struct.pack("<BBHQI", 14, 16, 0, gic[1][0], gic[1][1])
    return table("APIC", 6, body)


def make_fadt() -> bytes:
    body = bytearray(276 - 36)
    body[45 - 36] = 8  # Tablet
    struct.pack_into("<I", body, 112 - 36, 1 << 20)  # HW_REDUCED_ACPI only
    struct.pack_into("<H", body, 129 - 36, 1)  # PSCI compliant, SMC conduit
    body[131 - 36] = 5  # ACPI 6.5
    # FACS, X_DSDT, reset and sleep registers stay zero. AcpiTableDxe fixes
    # DSDT pointers; board reset uses the existing UEFI PSCI runtime service.
    return table("FACP", 6, body)


def make_gtdt(hw: dict) -> bytes:
    body = bytearray(60)
    struct.pack_into("<Q", body, 0, 0xffffffffffffffff)
    for i, interrupt in enumerate(hw["timer"]["interrupts"]):
        dt_flags = interrupt["dt_flags"]
        flags = (1 if dt_flags in (1, 2) else 0) | (2 if dt_flags in (2, 8) else 0)
        struct.pack_into("<II", body, 12 + i * 8, interrupt["gsi"], flags)
    struct.pack_into("<Q", body, 44, 0xffffffffffffffff)
    return table("GTDT", 2, body)


def make_dbg2(hw: dict) -> bytes:
    name = b"\\_SB.UAR0\0"
    # The device header is 22 bytes, including the size-offset field.
    device = struct.pack("<BHB9H", 0, 22 + 12 + 4 + len(name), 1,
                         len(name), 38, 0, 0, 0x8000, 0x0012, 0, 22, 34)
    device += struct.pack("<BBBBQ", 0, 32, 0, 3, hw["devices"]["uart0"]["reg"][0][0])
    device += struct.pack("<I", 0x1000) + name
    return table("DBG2", 0, struct.pack("<II", 44, 1) + device)


def make_spcr(hw: dict) -> bytes:
    # Revision 2 uses the DBG2 serial interface subtype values. MediaTek UART
    # subtype 0x12 is 32-bit MMIO with a register stride of four bytes.
    body = bytearray(80 - 36)
    body[0] = 0x12
    struct.pack_into("<BBBBQ", body, 4, 0, 32, 0, 3, hw["devices"]["uart0"]["reg"][0][0])
    body[16] = 8  # GIC
    struct.pack_into("<I", body, 18, hw["devices"]["uart0"]["gsi"])
    body[22] = 7  # 115200 baud
    body[24] = 1  # one stop bit; parity / flow control remain zero
    struct.pack_into("<HH", body, 28, 0xffff, 0xffff)
    return table("SPCR", 2, body)


def dsdt(smp: bool) -> str:
    cpus = "\n".join(f'''        Device (CPU{i}) {{
            Name (_HID, "ACPI0007")
            Name (_UID, {i})
            Name (_STA, {"0x0F" if smp or i == 0 else "Zero"})
        }}''' for i in range(8))
    return '''// SPDX-License-Identifier: BSD-2-Clause-Patent
DefinitionBlock ("", "DSDT", 2, "LNCLOT", "WOA6768 ", 1)
{
    Scope (\\_SB) {
''' + cpus + '''
        Device (UAR0) {
            // Debugger accesses use DBG2's MediaTek register layout. Avoid
            // binding a generic 8-bit, byte-stride COM driver to this UART.
            Name (_HID, "MTK8250")
            Name (_UID, Zero)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate () {
                Memory32Fixed (ReadWrite, 0x11002000, 0x1000)
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive) {144}
            })
        }
        // MTK MSDC is not SDHCI. No generic SDHCI compatible ID is advertised.
        Device (MMC0) {
            Name (_HID, "MTK6768")
            Name (_UID, Zero)
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate () {
                Memory32Fixed (ReadWrite, 0x11230000, 0x10000)
                Memory32Fixed (ReadWrite, 0x11CD0000, 0x1000)
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive) {132}
            })
        }
    }
}
'''


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--hardware", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()
    hw = json.loads(args.hardware.read_text())
    if hw["psci"]["method"] != "smc" or len(hw["cpus"]) != 8:
        raise ValueError("Unsupported board PSCI/CPU topology")
    for profile in ("up", "smp"):
        dest = args.out / profile
        dest.mkdir(parents=True, exist_ok=True)
        for name, data in {"APIC": make_madt(hw, profile == "smp"), "FACP": make_fadt(),
                           "GTDT": make_gtdt(hw), "DBG2": make_dbg2(hw), "SPCR": make_spcr(hw),
                           "LDBG": table("LDBG", 1, struct.pack("<IQIIQ", 1, 0x512f0000,
                                                                 0x1000, 0, 0x31474f4c414f574c))}.items():
            (dest / f"{name}.aml").write_bytes(data)
        (dest / "DSDT.asl").write_text(dsdt(profile == "smp"), encoding="ascii")
        subprocess.run(["iasl", "-ve", "-p", str(dest / "DSDT"), str(dest / "DSDT.asl")], check=True)
        report = []
        for path in sorted(dest.glob("*.aml")):
            data = path.read_bytes()
            assert struct.unpack_from("<I", data, 4)[0] == len(data) and sum(data) & 255 == 0
            report.append({"name": path.name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        (dest / "manifest.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
