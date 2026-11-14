// SPDX-License-Identifier: BSD-2-Clause-Patent
DefinitionBlock ("", "DSDT", 2, "LNCLOT", "WOA6768 ", 1)
{
    Scope (\_SB) {
        Device (CPU0) {
            Name (_HID, "ACPI0007")
            Name (_UID, 0)
            Name (_STA, 0x0F)
        }
        Device (CPU1) {
            Name (_HID, "ACPI0007")
            Name (_UID, 1)
            Name (_STA, 0x0F)
        }
        Device (CPU2) {
            Name (_HID, "ACPI0007")
            Name (_UID, 2)
            Name (_STA, 0x0F)
        }
        Device (CPU3) {
            Name (_HID, "ACPI0007")
            Name (_UID, 3)
            Name (_STA, 0x0F)
        }
        Device (CPU4) {
            Name (_HID, "ACPI0007")
            Name (_UID, 4)
            Name (_STA, 0x0F)
        }
        Device (CPU5) {
            Name (_HID, "ACPI0007")
            Name (_UID, 5)
            Name (_STA, 0x0F)
        }
        Device (CPU6) {
            Name (_HID, "ACPI0007")
            Name (_UID, 6)
            Name (_STA, 0x0F)
        }
        Device (CPU7) {
            Name (_HID, "ACPI0007")
            Name (_UID, 7)
            Name (_STA, 0x0F)
        }
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
        Device (LCD0) {
            // Native display resources are owned by the board-specific KMDOD.
            Name (_HID, "LNC0001")
            Name (_UID, Zero)
            Name (_CLS, Package () {0x03, Zero, Zero})
            Name (_STA, 0x0F)
            Name (_CRS, ResourceTemplate () {
                Memory32Fixed (ReadWrite, 0x7CDB0000, 0x02250000)
                Memory32Fixed (ReadWrite, 0x1400B000, 0x00002000)
            })
            Method (_DOD, 0, NotSerialized) { Return (Package () {0x80010400}) }
            Device (PNL0) {
                Name (_ADR, 0x80010400)
                Method (_DCS, 0, NotSerialized) { Return (0x1F) }
                Method (_DGS, 0, NotSerialized) { Return (One) }
            }
        }
// SPDX-License-Identifier: BSD-2-Clause-Patent
// Added to the existing DSDT _SB scope; no duplicate SSDT/LBAT owner.
// Read-only flags are in the existing 128-byte handoff's ACPI NVS page.
OperationRegion (WCFG, SystemMemory, 0x512F001C, 4)
Field (WCFG, DWordAcc, NoLock, Preserve) { WFLG, 32 }

Device (LBAT)
{
    Name (_HID, "LNB0001")
    Name (_UID, One)
    Name (_DDN, "Lancelot MT6358 Battery")
    Method (_STA, 0, NotSerialized) { Return (0x0F) }
    Method (BCFG, 0, NotSerialized) { If (And (WFLG, 0x20)) { Return (0x00010001) }
Return (Zero) }
    Name (_CRS, ResourceTemplate ()
    {
        Memory32Fixed (ReadWrite, 0x1000D000, 0x00001000)
    })
}

Device (SBKY)
{
    Name (_HID, "LNS0002")
    Name (_UID, One)
    Name (_DDN, "Lancelot Volume and Power Buttons")
    Method (_STA, 0, NotSerialized) { Return (0x0F) }
    Method (SCFG, 0, NotSerialized) { If (And (WFLG, 0x08)) { Return (0x00010002) }
Return (Zero) }
    Name (_CRS, ResourceTemplate ()
    {
        Memory32Fixed (ReadWrite, 0x10010000, 0x00001000)
    })
    // Firmware reads pin configuration only. No write or full GPIO
    // resource claim conflicts with the independent touch source.
    OperationRegion (GDIR, SystemMemory, 0x10005020, 4)
    Field (GDIR, DWordAcc, NoLock, Preserve) { DIR2, 32 }
    OperationRegion (GMOD, SystemMemory, 0x100053B0, 4)
    Field (GMOD, DWordAcc, NoLock, Preserve) { MODB, 32 }
    Method (KPIN, 0, Serialized)
    {
        Return (Or (And (ShiftRight (MODB, 20), 0x0F),
                    ShiftLeft (And (ShiftRight (DIR2, 29), One), 4)))
    }
}

Device (NTCH)
{
    Name (_HID, "LNT0001")
    Name (_UID, One)
    Name (_DDN, "Lancelot NT36672A Dijing Touch Source")
    Method (_STA, 0, NotSerialized)
    {
        Return (0x0F)
    }
    Method (TCFG, 0, Serialized)
    {
        If (And (WFLG, 0x10)) { Return (0x00010001) }
Return (Zero)
    }
    Name (_CRS, ResourceTemplate ()
    {
        Memory32Fixed (ReadWrite, 0x1100A000, 0x00001000) // SPI0
        Memory32Fixed (ReadWrite, 0x10005000, 0x00001000) // GPIO
        Memory32Fixed (ReadWrite, 0x10002800, 0x00000200) // IO_CFG_RM
        Memory32Fixed (ReadOnly,  0x10000000, 0x00001000) // TOPCKGEN
        Memory32Fixed (ReadWrite, 0x10001000, 0x00001000) // INFRACFG_AO
        Memory32Fixed (ReadOnly,  0x1000C000, 0x00001000) // APMIXED
    })
    // Polling version: no EINT parent/GIC interrupt, GPIO connection,
    // SPB connection or DMA resource is claimed by this source.
}


    }
}
