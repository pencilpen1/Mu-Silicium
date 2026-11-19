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
    // Added after the original 64-byte handoff; original field offsets stay.
    OperationRegion (CWPR, SystemMemory, 0x512F0140, 8)
    Field (CWPR, DWordAcc, NoLock, Preserve) { CWCT, 32, CWGE, 32 }
    Method (CWCF, 0, NotSerialized) { Return (CWCT) }
    Method (CWGN, 0, NotSerialized) { Return (CWGE) }
    // Separate finite-scope licence after old 72 bytes, old offsets preserved.
    OperationRegion (CIPR, SystemMemory, 0x512F0148, 12)
    Field (CIPR, DWordAcc, NoLock, Preserve) { CICT, 32, CIGE, 32, CIRE, 32 }
    Method (CICF, 0, NotSerialized) { Return (CICT) }
    Method (CIGN, 0, NotSerialized) { Return (CIGE) }
    Method (CIRC, 0, NotSerialized) { Return (CIRE) }
    Name (_CRS, ResourceTemplate ()
    {
        Memory32Fixed (ReadWrite, 0x11004000, 0x00001000)
    })
}
