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
            Name (_STA, Zero)
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
    }
}
