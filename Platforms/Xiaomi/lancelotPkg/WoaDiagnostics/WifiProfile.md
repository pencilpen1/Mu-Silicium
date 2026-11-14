# Exclusive Wi-Fi test profile

BuildWoa.py --profile smp --board-profile wifi selects Acpi/Wifi/smp. The default input-power profile retains the existing board devices. Each profile requires a separate boot and a matching PE test-profile.txt.

Wi-Fi owns MTKW6768/WFCV=1 and the supplied MMIO/IRQ 317 contract. LBAT, SBKY and NTCH are absent from its DSDT because their resource claims or PWRAP provider conflict with this first Wi-Fi implementation.

The existing MEM_RES/Reserv regions already cover DMA 0xbe000000/3 MiB (Runtime Reserved 11) and CONNSYS EMI 0xbf000000/4 MiB (Runtime Reserved 12). No extra reservation or RAM ceiling is added. CPU, EL2, display and MUSB handoff code is unchanged from the proven eight-core baseline.
