[Defines]
  PLATFORM_NAME                  = MT6768Pkg
  PLATFORM_GUID                  = 28f1a3bf-193a-47e3-a7b9-5a435eaab2ee
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010019
  OUTPUT_DIRECTORY               = Build/$(PLATFORM_NAME)
  SUPPORTED_ARCHITECTURES        = AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT
  FLASH_DEFINITION               = MT6768Pkg/MT6768Pkg.fdf

!include MT6768Pkg/MT6768Pkg.dsc

[PcdsFixedAtBuild.common]
  # DDR (FDT-dynamic MemoryPeim overrides at runtime; these are fallback)
  gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000
  gArmTokenSpaceGuid.PcdSystemMemorySize|0x100000000
  # UEFI own regions - placed in the runtime-free range [0x4d100000, 0x51340000)
  gEmbeddedTokenSpaceGuid.PcdPrePiStackBase|0x51300000
  gEmbeddedTokenSpaceGuid.PcdPrePiStackSize|0x00040000
  gMT6768PkgTokenSpaceGuid.PcdUefiMemPoolBase|0x4d300000
  gMT6768PkgTokenSpaceGuid.PcdUefiMemPoolSize|0x03fc0000
  gArmTokenSpaceGuid.PcdCpuVectorBaseAddress|0x512f0000
  # Runtime lancelot framebuffer (stride 1088, visible panel 1080x2340)
  gMT6768PkgTokenSpaceGuid.PcdMipiFrameBufferAddress|0x7cdb0000
  gMT6768PkgTokenSpaceGuid.PcdMipiFrameBufferWidth|1080
  gMT6768PkgTokenSpaceGuid.PcdMipiFrameBufferHeight|2340
  gMT6768PkgTokenSpaceGuid.PcdMipiFrameBufferVisibleWidth|1080
  gMT6768PkgTokenSpaceGuid.PcdMipiFrameBufferVisibleHeight|2340
  gMT6768PkgTokenSpaceGuid.PcdMipiFrameBufferPixelBpp|32
  gMT6768PkgTokenSpaceGuid.PcdMipiFrameBufferPixelsPerScanLine|1088
  gMT6768PkgTokenSpaceGuid.PcdMipiFrameBufferSize|0x02250000
