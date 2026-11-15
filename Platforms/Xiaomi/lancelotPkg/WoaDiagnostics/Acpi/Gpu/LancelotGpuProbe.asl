/* SPDX-License-Identifier: MIT
 * Standalone integration proposal, not installed in Mu-Silicium.
 * LNG0001 is a local development ID, not an existing firmware device.
 * Only GPU-owned blocks are described. Shared TOP/INFRA/SPM/APMIXED/PMIC
 * resources require a common power owner; no fake _PS0 readiness is supplied.
 */
DefinitionBlock ("", "SSDT", 2, "LNGWOA", "LGPROBE", 0x00000001)
{
    Scope (\_SB)
    {
        Device (LGP0)
        {
            Name (_HID, "LNG0001")
            Name (_UID, Zero)
            Name (_DDN, "Lancelot Mali-G52 resource diagnostic prototype")
            Name (_CCA, Zero)
            Method (_STA, 0, NotSerialized) { Return (0x0F) }
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x13040000, 0x00004000)
                Memory32Fixed (ReadWrite, 0x13000000, 0x00001000)
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive) { 306 } // GPU
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive) { 307 } // MMU
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive) { 308 } // JOB
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive) { 309 } // EVENT
                Interrupt (ResourceConsumer, Level, ActiveLow, Exclusive) { 310 } // PWR
            })
        }
    }
}
