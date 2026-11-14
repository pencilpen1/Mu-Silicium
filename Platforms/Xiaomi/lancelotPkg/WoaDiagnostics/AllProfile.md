# Four-driver integration profile

BuildWoa.py --profile smp --board-profile all enumerates LBAT, SBKY, NTCH and WLAN in one boot, as requested by the user. Use a matching PE selector containing all.

The four supplied device resource contracts are retained byte for byte. PWRAP and GPIO/clock ranges overlap; Windows resource arbitration and each driver's original checks decide which devices can start. No shared PWRAP bus provider has been introduced. Runtime logs must distinguish installed, started and functional devices, and report resource failures explicitly.

Eight-core topology, EL2, memory, display and MUSB code match the proven baseline. The required Wi-Fi DRAM ranges are already reserved; no RAM ceiling is added.
