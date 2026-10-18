/** @file

  Copyright (c) 2011-2017, ARM Limited. All rights reserved.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiPei.h>

#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugAgentLib.h>
#include <Library/PrePiLib.h>
#include <Library/PrePiHobListPointerLib.h>
#include <Library/TimerLib.h>
#include <Library/PerformanceLib.h>
#include <Library/IoLib.h>
#include <Library/BootScreen.h>

#include <Ppi/GuidedSectionExtraction.h>
#include <Ppi/ArmMpCoreInfo.h>
#include <Ppi/SecPerformance.h>

#include "PrePi.h"

// FDT pointer passed by LK in x0 (saved by ModuleEntryPoint.S, forwarded
// through CEntryPoint). Used by MemoryInitPeiLib to build the memory map
// dynamically. NULL if LK did not provide one.
VOID *gFdtBase = NULL;

VOID EFIAPI ProcessLibraryConstructorList(VOID);

VOID
EFIAPI
SecCommonExceptionEntry (
  IN UINT32 Type,
  IN UINTN  Elr,
  IN UINTN  Esr,
  IN UINTN  Far
  )
{
  STATIC volatile BOOLEAN HandlingFault;

  if (HandlingFault) {
    CpuDeadLoop ();
  }
  HandlingFault = TRUE;
  BootScreenHex (128, "EXC=", Type, 0xff500000U);
  BootScreenHex (176, "ELR=", Elr, 0xff500000U);
  BootScreenHex (224, "ESR=", Esr, 0xff500000U);
  BootScreenHex (272, "FAR=", Far, 0xff500000U);
  CpuDeadLoop ();
}

// LK's MTK WDT path programs MODE at 0x10007000 with key 0x22000000.
// Disable only the AP watchdog here; UEFI has no MTK watchdog driver to kick it.
STATIC
VOID
DisableApWatchdog (
  VOID
  )
{
  UINT32 Mode;

  Mode = MmioRead32 (0x10007000);
  MmioWrite32 (0x10007000, (Mode & ~BIT0) | 0x22000000U);
}

VOID Main (IN  UINT64  StartTimeStamp)
{
  EFI_HOB_HANDOFF_INFO_TABLE  *HobList;
  EFI_STATUS                  Status;

  EFI_PHYSICAL_ADDRESS MemoryBase     = 0;
  UINT64               MemorySize     = 0;
  EFI_PHYSICAL_ADDRESS UefiMemoryBase = 0;
  UINT64               UefiMemorySize = 0;
  EFI_PHYSICAL_ADDRESS StacksBase     = 0;
  UINT64               StacksSize     = 0;
  
  // Architecture-specific initialization
  // Enable Floating Point
  ArmEnableVFP();

  /* Enable program flow prediction, if supported */
  ArmEnableBranchPrediction();

  // Declare UEFI region
  MemoryBase     = FixedPcdGet64(PcdSystemMemoryBase);
  MemorySize     = FixedPcdGet64(PcdSystemMemorySize);
  UefiMemoryBase = FixedPcdGet64(PcdUefiMemPoolBase);
  UefiMemorySize = FixedPcdGet64(PcdUefiMemPoolSize);
  StacksBase     = FixedPcdGet64(PcdPrePiStackBase);
  StacksSize     = FixedPcdGet64(PcdPrePiStackSize);

  DEBUG(
      (EFI_D_INFO | EFI_D_LOAD,
       "UEFI Memory Base = 0x%llx, Size = 0x%llx, Stack Base = 0x%llx, Stack "
       "Size = 0x%llx\n",
       (VOID *)(UINTN)UefiMemoryBase, UefiMemorySize,
       (VOID *)(UINTN)StacksBase, StacksSize));

  // Declare the PI/UEFI memory region
  // Set up HOB
  BootScreenStage ('S', 3); // entering HOB construction
  HobList = HobConstructor(
      (VOID *)UefiMemoryBase, UefiMemorySize, (VOID *)UefiMemoryBase,
      (VOID *)(UINTN)(UefiMemoryBase + UefiMemorySize));
  PrePeiSetHobList (HobList);

  // Initialize MMU
  BootScreenStage ('S', 4); // HOB ready, entering memory/FDT setup
  Status = MemoryPeim(UefiMemoryBase, UefiMemorySize);

  if (EFI_ERROR(Status)) {
    DEBUG((EFI_D_ERROR, "Failed to configure MMU\n"));
    BootScreenHex (128, "MEM ERR=", Status, 0xff500000U);
    CpuDeadLoop();
  }
  BootScreenStage ('S', 5); // MMU and memory HOBs complete

  DEBUG((EFI_D_LOAD | EFI_D_INFO, "MMU configured from device config\n"));

  // Add HOBs
  BuildStackHob ((UINTN)StacksBase, StacksSize);

  // TODO: Call CpuPei as a library
  BuildCpuHob (40, PcdGet8 (PcdPrePiCpuIoSize));

  // Set the Boot Mode
  SetBootMode (BOOT_WITH_FULL_CONFIGURATION);

  // Initialize Platform HOBs (CpuHob and FvHob)
  Status = PlatformPeim ();
  ASSERT_EFI_ERROR (Status);
  BootScreenStage ('S', 6); // platform HOBs complete

  // Now, the HOB List has been initialized, we can register performance information
  //PERF_START (NULL, "PEI", NULL, StartTimeStamp);

  // SEC phase needs to run library constructors by hand.
  ProcessLibraryConstructorList ();
  BootScreenStage ('S', 7); // PEI constructors complete

  // Assume the FV that contains the SEC (our code) also contains a compressed FV.
  Status = DecompressFirstFv ();
  if (EFI_ERROR (Status)) {
    BootScreenHex (128, "FV ERR=", Status, 0xff500000U);
    CpuDeadLoop ();
  }
  BootScreenStage ('S', 8); // FV decompression complete, loading DXE

  // Load the DXE Core and transfer control to it
  Status = LoadDxeCoreFromFv (NULL, 0);
  BootScreenHex (128, "DXE RET=", Status, 0xff500000U);
  CpuDeadLoop ();
}

VOID
CEntryPoint (IN UINTN FdtPointer)
{
  BootLogInitialize (GetPerformanceCounter ());
  BootLogPrint ("Lancelot UEFI boot log v1 (USB diagnostics 0.3)\n");
  BootLogPrint ("SEC C entry, FDT=0x%lx\n", (UINT64)FdtPointer);
  gFdtBase = (VOID *)FdtPointer;
  BootScreenStage ('S', 1);
  BootScreenText (64, "AUDIT2 - DXE NOT ENTERED", 0xff202020U);
  DisableApWatchdog ();
  // SEC deliberately does not initialize SerialPortLib.  The framebuffer
  // stage markers do not depend on UART or the text renderer.
  BootScreenStage ('S', 2); // watchdog disabled
  Main(0);
}
