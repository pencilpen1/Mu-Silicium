# Lancelot GPU device-mapping contract and power/ID diagnostic

## Firmware changes

The board firmware now maps these three additional MMIO windows as uncached
`NS_DEVICE` / memory-mapped I/O, not RAM:

| Name | Base | Size |
|---|---:|---:|
| SPM Control | 0x10006000 | 0x1000 |
| MFG Configuration | 0x13000000 | 0x1000 |
| Mali Registers | 0x13040000 | 0x4000 |

`PmicWrapperDxe` publishes `gMediaTekGpuMapProtocolGuid`
(`52ac17b1-b103-4e02-923f-618b37a76801`) only after verifying all six required
SPM/INFRA/MFGCFG/GPU/TOP/APMIXED named mapping descriptors and sizes. The data
structure describes mappings only, not power-on state, DEVAPC permission bypass,
or AP/secure-world ownership. Publication does not read or write the GPU.
Absent/incorrect metadata does not disable the normal PMIC protocol.

There is one implementation; no Windows/Linux/iOS compatibility-mode branch.
These mappings do not start the GPU on ordinary OS boot. A separate opt-in loader
diagnostic performs the power sequence, using the actual bounded firmware PMIC
read protocol and the shared portable GPU platform lifecycle. It preserves live
VGPU/VSRAM_GPU rails and does not issue PMIC register writes or change PLLs.

## Validation and measured scope

- Actual PWRAP source/map-contract host fixtures passed GCC/Clang ASan/UBSan,
  including missing/wrong-size map rejection and publication without GPU MMIO.
- Formal AARCH64 RELEASE build/link/FV generation passed. An initial compile
  failed because the mapping helper takes a mutable name; mutable fixed-size
  name arrays corrected the interface without changing that helper. The failed
  log/source and successful retry are retained privately.
- Successful FD: 2,097,152 bytes, SHA-256
  `4542cd331ec9bf8828c20bdec8b6f43657d93af78905d397cbd0703ea55dd32d`.
- The Boot-v2 template preserved all unrelated header/ramdisk/DTB sections.
  Flashed image: 608,256 bytes, SHA-256
  `ac7b0f8f18eb972e08e7f4df08bbf47bf39f5eb0a1c7403d2e91c1289e4fcdfe`.
- The bounded loader power-on/ID/power-off test completed on real Lancelot:
  `GPU_ID=0x74021000`, `STATUS=0`, source clock encoding 534,999,909 Hz,
  `started=1`, `off-restored=1`, `teardown=0`, 32 allowlisted writes.
  SWCID was `0x5820`; VGPU/VSRAM DA encodings remained 800/900 mV.
  These voltage encodings are not electrical measurements.
- The subsequent XNU observation again showed all four MFG domains off, with
  MFG bus protection set. Finite capture had no panic signature, CRC/length
  error or ring gap. Touch/CPU regressions were not rerun in this GPU-only round.

No GPU job/DMA, shader execution, pixels, Mesa, IOSurface, CoreAnimation or Metal
acceptance is claimed. The diagnostic does not install an XNU WACS2 ownership
provider. The GPU is **off after the diagnostic**, not available for later XNU
jobs merely because its ID was read earlier.

Windows/Linux ACPI/DT/boot recipes, storage, reserved RAM and existing build-script
work were not changed by this patch. Real Windows/Linux boot regression remains
unperformed for this firmware. Original working Mu/Android Boot and raw evidence
are retained privately; no firmware binaries or raw account/device logs are
published.
