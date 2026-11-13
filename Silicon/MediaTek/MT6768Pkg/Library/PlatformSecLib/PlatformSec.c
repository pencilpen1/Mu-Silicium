/**
  Copyright (c) 2011-2012, ARM Limited. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/IoLib.h>
#include <Library/BootLogLib.h>
#include <Library/TimerLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/PlatformSecLib.h>

#include "PlatformRegisters.h"

VOID
DisableWatchDogTimer ()
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR WDogRegion;

  // Locate "WatchDog Timer" Memory Region
  Status = LocateMemoryRegionByName ("WatchDog Timer", &WDogRegion);
  if (EFI_ERROR (Status)) {
    return;
  }

  // Disable WatchDog Timer
  MmioWrite32 (WDogRegion.Address, WDT_MODE_KEY);
}

STATIC
BOOLEAN
FixBootSurface (
  UINTN Base,
  UINT32 ConOffset,
  UINT32 AddrOffset,
  EFI_MEMORY_REGION_DESCRIPTOR *FrameBuffer)
{
  UINT32 Address = MmioRead32 (Base + AddrOffset);
  UINT32 Con = MmioRead32 (Base + ConOffset);
  UINT32 Pitch = MmioRead32 (Base + ConOffset + 0x14);
  UINT32 Size = MmioRead32 (Base + ConOffset + 8);
  BootLogPrint ("[WOA][DISPLAY] layer con=0x%x addr=%08x pitch=%08x size=%08x control=%08x\n",
                ConOffset, Address, Pitch, Size, Con);
  // Only the active RGB surface whose memory and geometry match this board's
  // captured framebuffer may be changed. Keep other overlays untouched.
  if ((UINT64)Address < FrameBuffer->Address ||
      (UINT64)Address >= FrameBuffer->Address + FrameBuffer->Length ||
      (Pitch & 0xffff) != 1088 * 4 ||
      (Size & 0xffff) != 1080 || (Size >> 16) != 2340 ||
      ((Con >> 28) & 3) != 0)
    return FALSE;
  // MT6768 has no XRGB input format. CONST_BLD selects constant alpha instead
  // of interpreting the reserved X byte written by Windows BasicDisplay.
  UINT32 NewCon = (Con & ~BIT8) | 0xff;
  UINT32 NewPitch = Pitch | BIT28;
  MmioWrite32 (Base + ConOffset, NewCon);
  MmioWrite32 (Base + ConOffset + 0x14, NewPitch);
  BootLogPrint ("[WOA][DISPLAY] opaque boot surface base=0x%lx layer=0x%x control=%08x pitch=%08x\n",
                (UINT64)Base, ConOffset, MmioRead32 (Base + ConOffset),
                MmioRead32 (Base + ConOffset + 0x14));
  return TRUE;
}

STATIC
UINT32
FixBootEngine (
  UINTN Base,
  UINT32 Layers,
  EFI_MEMORY_REGION_DESCRIPTOR *FrameBuffer)
{
  UINT32 Source = MmioRead32 (Base + 0x2c);
  UINT32 Extension = MmioRead32 (Base + 0x324);
  UINT32 Matched = 0;
  BootLogPrint ("[WOA][DISPLAY] engine=0x%lx source=%08x extension=%08x\n",
                (UINT64)Base, Source, Extension);
  for (UINT32 I = 0; I < Layers; I++) {
    if ((Source & (1U << I)) != 0)
      Matched += FixBootSurface (Base, 0x30 + 0x20 * I, 0xf40 + 0x20 * I, FrameBuffer);
  }
  for (UINT32 I = 0; I < 3; I++) {
    if ((Extension & (1U << I)) != 0)
      Matched += FixBootSurface (Base, 0x330 + 0x20 * I, 0xfb0 + 4 * I, FrameBuffer);
  }
  return Matched;
}

VOID
EnableConstantBlending ()
{
  EFI_MEMORY_REGION_DESCRIPTOR Ovl, FrameBuffer;
  if (EFI_ERROR (LocateMemoryRegionByName ("Display Reserved", &FrameBuffer)) ||
      EFI_ERROR (LocateMemoryRegionByName ("Display OVL", &Ovl)))
    return;
  UINT32 Matched = FixBootEngine ((UINTN)Ovl.Address, 4, &FrameBuffer);
  if (Matched == 0 && !EFI_ERROR (LocateMemoryRegionByName ("Display OVL 2L", &Ovl)))
    Matched = FixBootEngine ((UINTN)Ovl.Address, 2, &FrameBuffer);
  BootLogPrint ("[WOA][DISPLAY] matched boot surfaces=%u\n", Matched);
}

VOID
PlatformInitialize ()
{
  BootLogInitialize (GetPerformanceCounter ());
  BootLogPrint ("[WOA] SEC entry\n");
  // Disable WatchDog Timer
  DisableWatchDogTimer ();

  // Enable Constant Blending
  EnableConstantBlending ();
}
