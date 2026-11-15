# boot20 GPU, WLAN and charger diagnostic integration

Use BuildWoa.py --board-profile all --profile smp. The additional GPU SSDT declares only LNG0001 and its MFG/GPU/IRQ resources; display remains LNC0001. The GPU Windows package has no shared power provider; default cache/ABI tests do not request identity probing or GPU register access.

WLAN contract v2 removes its duplicate TOPCKGEN/GPIO/INFRACFG/PWRAP claims and names NTCH/LBAT dependencies. Windows providers are not supplied; this is a resource/requirements diagnostic test, not a complete WLAN bring-up. Existing touch and battery resource declarations and drivers are retained.

The supplied charger evidence gate keeps production profile/current proof empty, logs the missing evidence, and publishes only diagnostic permit 0x31. No SMB1351 charge writes or START/STOP are enabled. Eight cores and full memory policy are retained. BOOT is written only using native fastboot.
