// SPDX-License-Identifier: BSD-2-Clause-Patent
// Additive polling contract; values reflect runtime preparation in ACPI NVS.
Device (LCHG)
{
    // LNC0001 belongs to the already deployed display device.
    Name (_HID, "LCH0001")
    Name (_UID, One)
    Name (_DDN, "Lancelot SMB1351 Charger")
    OperationRegion (CFGR, SystemMemory, 0x512F0100, 8)
    Field (CFGR, DWordAcc, NoLock, Preserve) { CFRD, 32, CFHZ, 32 }
    Method (_STA, 0, NotSerialized) { Return (0x0F) }
    Method (CCFG, 0, NotSerialized) { Return (CFRD) }
    Method (CFQ0, 0, NotSerialized) { Return (CFHZ) }
    Method (CDIV, 0, NotSerialized) { Return (5) }
    Method (CMAX, 0, NotSerialized) { Return (4400) }
    Name (_CRS, ResourceTemplate ()
    {
        Memory32Fixed (ReadWrite, 0x11004000, 0x00001000)
    })
}
