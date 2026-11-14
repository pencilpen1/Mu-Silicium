// SPDX-License-Identifier: BSD-2-Clause-Patent
// Exclusive cold-boot Wi-Fi resources from the supplied contract.
// LBAT/SBKY/NTCH are absent in this profile to avoid shared WACS2/MMIO owners.
Device (WLAN)
        {
            Name (_HID, "MTKW6768")
            Name (_UID, 0)
            Name (_DDN, "Lancelot CONNAC E1 AXI Wi-Fi")
            Name (_STA, 0x0F)
            // Fixed resource contract v1. The lancelot UEFI memory map must
            // reserve the two DRAM ranges below before Windows starts.
            Method (WFCV, 0, NotSerialized) { Return (One) }
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x18000000, 0x00100000)
                Memory32Fixed (ReadWrite, 0x10000000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x10001000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x10005000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x10006000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x10007000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x10002C00, 0x00000100)
                Memory32Fixed (ReadWrite, 0x1000D000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x1100C000, 0x00001000)
                Memory32Fixed (ReadWrite, 0x0C53A650, 0x00000050)
                Memory32Fixed (ReadWrite, 0xBE000000, 0x00300000)
                Memory32Fixed (ReadWrite, 0xBF000000, 0x00400000)
                // SYSIRQ bit 29 inverts the CONNAC active-low line before GIC.
                // DT SPI 285 becomes GIC INTID/GSI 317 (0x13D).
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive)
                {
                    0x0000013D
                }
            })
        }
