/* SimpleFbDxe: Simple FrameBuffer */
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include "Library/FrameBufferBltLib.h"
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <PiDxe.h>
#include <Protocol/GraphicsOutput.h>
#include <Uefi.h>

/// Defines
/*
 * Convert enum video_log2_bpp to bytes and bits. Note we omit the outer
 * brackets to allow multiplication by fractional pixels.
 */
#define VNBYTES(bpix) (1 << (bpix)) / 8
#define VNBITS(bpix) (1 << (bpix))

#define FB_BITS_PER_PIXEL (32)
#define FB_BYTES_PER_PIXEL (FB_BITS_PER_PIXEL / 8)

/*
 * Bits per pixel selector. Each value n is such that the bits-per-pixel is
 * 2 ^ n
 */
enum video_log2_bpp {
  VIDEO_BPP1 = 0,
  VIDEO_BPP2,
  VIDEO_BPP4,
  VIDEO_BPP8,
  VIDEO_BPP16,
  VIDEO_BPP32,
};

typedef struct {
  VENDOR_DEVICE_PATH DisplayDevicePath;
  EFI_DEVICE_PATH    EndDevicePath;
} DISPLAY_DEVICE_PATH;

DISPLAY_DEVICE_PATH mDisplayDevicePath = {
    {{HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
          (UINT8)(sizeof(VENDOR_DEVICE_PATH)),
          (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8),
      }},
     EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID},
    {END_DEVICE_PATH_TYPE,
     END_ENTIRE_DEVICE_PATH_SUBTYPE,
     {sizeof(EFI_DEVICE_PATH_PROTOCOL), 0}}};

/// Declares

STATIC FRAME_BUFFER_CONFIGURE *mFrameBufferBltLibConfigure;
STATIC UINTN                   mFrameBufferBltLibConfigureSize;

STATIC
EFI_STATUS
EFIAPI
DisplayQueryMode(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber,
    OUT UINTN *SizeOfInfo, OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);

STATIC
EFI_STATUS
EFIAPI
DisplaySetMode(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber);

STATIC
EFI_STATUS
EFIAPI
DisplayBlt(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *            BltBuffer,
    OPTIONAL IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
    IN UINTN SourceX, IN UINTN SourceY, IN UINTN DestinationX,
    IN UINTN DestinationY, IN UINTN Width, IN UINTN Height,
    IN UINTN Delta OPTIONAL);

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL mDisplay = {
    DisplayQueryMode, DisplaySetMode, DisplayBlt, NULL};

STATIC
EFI_STATUS
EFIAPI
DisplayQueryMode(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber,
    OUT UINTN *SizeOfInfo, OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info)
{
  EFI_STATUS Status;
  if ((This == NULL) || (SizeOfInfo == NULL) || (Info == NULL) || (ModeNumber != 0)) {
    return EFI_INVALID_PARAMETER;
  }
  Status = gBS->AllocatePool(
      EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
      (VOID **)Info);

  if (EFI_ERROR (Status)) {
    return Status;
  }
  *SizeOfInfo                   = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  (*Info)->Version              = This->Mode->Info->Version;
  (*Info)->HorizontalResolution = This->Mode->Info->HorizontalResolution;
  (*Info)->VerticalResolution   = This->Mode->Info->VerticalResolution;
  (*Info)->PixelFormat          = This->Mode->Info->PixelFormat;
  (*Info)->PixelInformation     = This->Mode->Info->PixelInformation;
  (*Info)->PixelsPerScanLine    = This->Mode->Info->PixelsPerScanLine;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DisplaySetMode(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber)
{
  if (ModeNumber != 0) {
    return EFI_UNSUPPORTED;
  }
  SetMem32 ((VOID *)(UINTN)This->Mode->FrameBufferBase,
            This->Mode->FrameBufferSize, 0xff000000U);
  WriteBackDataCacheRange ((VOID *)(UINTN)This->Mode->FrameBufferBase,
                          This->Mode->FrameBufferSize);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
DisplayBlt(
    IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL *            BltBuffer,
    OPTIONAL IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
    IN UINTN SourceX, IN UINTN SourceY, IN UINTN DestinationX,
    IN UINTN DestinationY, IN UINTN Width, IN UINTN Height,
    IN UINTN Delta OPTIONAL)
{

  RETURN_STATUS Status;
  EFI_TPL       Tpl;

  if ((This == NULL) || (This->Mode == NULL) || (This->Mode->Info == NULL) ||
      (BltOperation >= EfiGraphicsOutputBltOperationMax) ||
      (Width == 0) || (Height == 0) ||
      ((BltOperation != EfiBltVideoToVideo) && (BltBuffer == NULL))) {
    return EFI_INVALID_PARAMETER;
  }
  if ((BltOperation != EfiBltVideoToBltBuffer) &&
      ((DestinationX >= This->Mode->Info->HorizontalResolution) ||
       (DestinationY >= This->Mode->Info->VerticalResolution) ||
       (Width > This->Mode->Info->HorizontalResolution - DestinationX) ||
       (Height > This->Mode->Info->VerticalResolution - DestinationY))) {
    return EFI_INVALID_PARAMETER;
  }
  if (((BltOperation == EfiBltVideoToVideo) || (BltOperation == EfiBltVideoToBltBuffer)) &&
      ((SourceX >= This->Mode->Info->HorizontalResolution) ||
       (SourceY >= This->Mode->Info->VerticalResolution) ||
       (Width > This->Mode->Info->HorizontalResolution - SourceX) ||
       (Height > This->Mode->Info->VerticalResolution - SourceY))) {
    return EFI_INVALID_PARAMETER;
  }
  //
  // We have to raise to TPL_NOTIFY, so we make an atomic write to the frame
  // buffer. We would not want a timer based event (Cursor, ...) to come in
  // while we are doing this operation.
  //
  Tpl    = gBS->RaiseTPL(TPL_NOTIFY);
  Status = FrameBufferBlt(
      mFrameBufferBltLibConfigure, BltBuffer, BltOperation, SourceX, SourceY,
      DestinationX, DestinationY, Width, Height, Delta);
  if (!RETURN_ERROR (Status) && (BltOperation != EfiBltVideoToBltBuffer)) {
    // GOP's reserved byte is not an alpha contract. LK may leave per-pixel
    // alpha enabled on its scanout layer; match the working opaque debug text.
    // The BLT library has validated the destination rectangle at this point.
    UINTN Stride = This->Mode->Info->PixelsPerScanLine;
    UINT32 *Pixels = (UINT32 *)(UINTN)This->Mode->FrameBufferBase;
    for (UINTN Y = DestinationY; Y < DestinationY + Height; Y++) {
      for (UINTN X = DestinationX; X < DestinationX + Width; X++) {
        Pixels[Y * Stride + X] |= 0xff000000U;
      }
    }
    // Publish only the touched rows, before allowing another console writer.
    WriteBackDataCacheRange (Pixels + DestinationY * Stride,
                            Height * Stride * sizeof (UINT32));
  }
  gBS->RestoreTPL(Tpl);

  return Status;
}

EFI_STATUS
EFIAPI
SimpleFbDxeInitialize(
    IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable)
{

  EFI_STATUS Status             = EFI_SUCCESS;
  EFI_HANDLE hUEFIDisplayHandle = NULL;

  /* Retrieve simple frame buffer from pre-SEC bootloader */
  DEBUG(
      (EFI_D_ERROR,
       "SimpleFbDxe: Retrieve MIPI FrameBuffer parameters from PCD\n"));
  UINT32 MipiFrameBufferAddr   = FixedPcdGet32(PcdMipiFrameBufferAddress);
  UINT32 MipiFrameBufferWidth  = FixedPcdGet32(PcdMipiFrameBufferWidth);
  UINT32 MipiFrameBufferHeight = FixedPcdGet32(PcdMipiFrameBufferHeight);

  /* Sanity check */
  if (MipiFrameBufferAddr == 0 || MipiFrameBufferWidth == 0 ||
      MipiFrameBufferHeight == 0) {
    DEBUG((EFI_D_ERROR, "SimpleFbDxe: Invalid FrameBuffer parameters\n"));
    return EFI_DEVICE_ERROR;
  }

  /* Prepare struct */
  if (mDisplay.Mode == NULL) {
    Status = gBS->AllocatePool(
        EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE),
        (VOID **)&mDisplay.Mode);

    ASSERT_EFI_ERROR(Status);
    if (EFI_ERROR(Status))
      return Status;

    ZeroMem(mDisplay.Mode, sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE));
  }

  if (mDisplay.Mode->Info == NULL) {
    Status = gBS->AllocatePool(
        EfiBootServicesData, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
        (VOID **)&mDisplay.Mode->Info);

    ASSERT_EFI_ERROR(Status);
    if (EFI_ERROR(Status))
      return Status;

    ZeroMem(mDisplay.Mode->Info, sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION));
  }

  /* Set information */
  mDisplay.Mode->MaxMode       = 1;
  mDisplay.Mode->Mode          = 0;
  mDisplay.Mode->Info->Version = 0;

  mDisplay.Mode->Info->HorizontalResolution = MipiFrameBufferWidth;
  mDisplay.Mode->Info->VerticalResolution   = MipiFrameBufferHeight;

  /* SimpleFB runs on a8r8g8b8 (VIDEO_BPP32) for DB410c */
  UINT32               PixelsPerScanLine = FixedPcdGet32(PcdMipiFrameBufferPixelsPerScanLine);
  UINT32               FrameBufferSize = FixedPcdGet32(PcdMipiFrameBufferSize);
  EFI_PHYSICAL_ADDRESS FrameBufferAddress = MipiFrameBufferAddr;

  mDisplay.Mode->Info->PixelsPerScanLine = PixelsPerScanLine;
  mDisplay.Mode->Info->PixelFormat = PixelRedGreenBlueReserved8BitPerColor;
  mDisplay.Mode->SizeOfInfo      = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
  mDisplay.Mode->FrameBufferBase = FrameBufferAddress;
  if ((PixelsPerScanLine < MipiFrameBufferWidth) ||
      ((UINT64)PixelsPerScanLine * MipiFrameBufferHeight * 4 > FrameBufferSize)) {
    return EFI_DEVICE_ERROR;
  }
  // GOP exposes one visible surface, not the whole LK reservation.
  FrameBufferSize = PixelsPerScanLine * MipiFrameBufferHeight * 4;
  mDisplay.Mode->FrameBufferSize = FrameBufferSize;

  //
  // Create the FrameBufferBltLib configuration.
  //
  Status = FrameBufferBltConfigure(
      (VOID *)(UINTN)mDisplay.Mode->FrameBufferBase, mDisplay.Mode->Info,
      mFrameBufferBltLibConfigure, &mFrameBufferBltLibConfigureSize);
  if (Status == RETURN_BUFFER_TOO_SMALL) {
    mFrameBufferBltLibConfigure = AllocatePool(mFrameBufferBltLibConfigureSize);
    if (mFrameBufferBltLibConfigure != NULL) {
      Status = FrameBufferBltConfigure(
          (VOID *)(UINTN)mDisplay.Mode->FrameBufferBase, mDisplay.Mode->Info,
          mFrameBufferBltLibConfigure, &mFrameBufferBltLibConfigureSize);
    }
  }
  ASSERT_EFI_ERROR(Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // zhuowei: clear the screen to black
  // UEFI standard requires this, since text is white - see
  // OvmfPkg/QemuVideoDxe/Gop.c
  SetMem32((VOID *)(UINTN)FrameBufferAddress, FrameBufferSize, 0xff000000U);
  // hack: clear cache
  WriteBackInvalidateDataCacheRange(
      (void *)FrameBufferAddress, FrameBufferSize);
  // zhuowei: end

  /* Register handle */
  Status = gBS->InstallMultipleProtocolInterfaces(
      &hUEFIDisplayHandle, &gEfiDevicePathProtocolGuid, &mDisplayDevicePath,
      &gEfiGraphicsOutputProtocolGuid, &mDisplay, NULL);

  ASSERT_EFI_ERROR(Status);

  return Status;
}
