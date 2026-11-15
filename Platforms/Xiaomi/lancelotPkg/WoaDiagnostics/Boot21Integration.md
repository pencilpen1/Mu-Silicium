# boot21 GPU, WLAN and charger diagnostics

BuildWoa.py --board-profile all --profile smp retains the eight-core MADT, full memory policy, GPU SSDT, WLAN resource contract v2 and accepted touch/battery/display/button resources. GPU 0.2.1.0 updates the delivered Windows loader contract and serial default tests; no extra SSDT or GPU power provider is added. WLAN 0.1.6.0 contains owner review proposals, which are not formal touch/battery releases and are not deployed.

The supplied charger firmware proposal only logs the calculated start/stop permission and outstanding evidence. It does not modify SMB1351 configuration, grant charging authority, or change CCFG 0x10031 and production profile 0. The separately built Windows MUSB owner publishes USB observations with capabilities 0x11, without grant, Sink or VBUS proof. Charger 0.4.0.0 is tested with ABI4 refusal PLAN and read-only battery power observations. No START/STOP is requested.
