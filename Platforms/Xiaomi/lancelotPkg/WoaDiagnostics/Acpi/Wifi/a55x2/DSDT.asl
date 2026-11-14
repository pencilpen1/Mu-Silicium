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
            Name (_STA, Zero)
        }
        Device (CPU3) {
            Name (_HID, "ACPI0007")
            Name (_UID, 3)
            Name (_STA, Zero)
        }
        Device (CPU4) {
            Name (_HID, "ACPI0007")
            Name (_UID, 4)
            Name (_STA, Zero)
        }
        Device (CPU5) {
            Name (_HID, "ACPI0007")
            Name (_UID, 5)
            Name (_STA, Zero)
        }
        Device (CPU6) {
            Name (_HID, "ACPI0007")
            Name (_UID, 6)
            Name (_STA, Zero)
        }
        Device (CPU7) {
            Name (_HID, "ACPI0007")
            Name (_UID, 7)
            Name (_STA, Zero)
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

    }
}
