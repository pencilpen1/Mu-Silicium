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

