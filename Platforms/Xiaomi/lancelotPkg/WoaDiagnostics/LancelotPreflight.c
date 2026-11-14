/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Save the firmware state on the boot volume, then start Windows Boot Manager.
 */
#include <Uefi.h>
#include <Guid/Acpi.h>
#include <Guid/FileInfo.h>
#include <IndustryStandard/Acpi65.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ArmSmcLib.h>
#include <Library/BootLogLib.h>
#include <Library/DevicePathLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include "lancelot_handoff.h"

STATIC EFI_FILE_PROTOCOL *mLog;
STATIC EFI_FILE_PROTOCOL *mDir;
STATIC volatile LANCELOT_HANDOFF *mHandoff = (VOID *)(UINTN)LANCELOT_HANDOFF_BASE;

STATIC VOID Log (CONST CHAR8 *Format, ...)
{
  CHAR8 Buffer[512];
  VA_LIST Marker;
  UINTN Size;
  VA_START (Marker, Format);
  Size = AsciiVSPrint (Buffer, sizeof (Buffer), Format, Marker);
  VA_END (Marker);
  BootLogWrite (Buffer, Size);
  if (mLog != NULL) mLog->Write (mLog, &Size, Buffer);
}

STATIC EFI_STATUS OpenNew (EFI_FILE_PROTOCOL *Parent, CHAR16 *Name, EFI_FILE_PROTOCOL **File)
{
  return Parent->Open (Parent, File, Name,
    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
}

STATIC VOID DumpCpuTopology (EFI_ACPI_DESCRIPTION_HEADER *Table)
{
  UINTN Offset = 44;
  UINT32 Present = 0, Enabled = 0, Count = 0;
  CHAR8 *Profile;
  if (Table->Length < 44 || Table->Length > 4096) {
    Log ("FAIL: MADT CPU topology length invalid\r\n");
    return;
  }
  while (Offset < Table->Length) {
    UINT8 *Entry = (UINT8 *)Table + Offset;
    UINTN Length;
    if (Table->Length - Offset < 2 || Entry[1] < 2 || Entry[1] > Table->Length - Offset) {
      Log ("FAIL: MADT CPU entry boundary invalid\r\n");
      return;
    }
    Length = Entry[1];
    if (Entry[0] == 11) {
      UINT32 Uid, Flags;
      UINT64 Mpidr, Gicr;
      if (Length != 82) { Log ("FAIL: MADT GICC length invalid\r\n"); return; }
      CopyMem (&Uid, Entry + 8, sizeof (Uid));
      CopyMem (&Flags, Entry + 12, sizeof (Flags));
      CopyMem (&Gicr, Entry + 60, sizeof (Gicr));
      CopyMem (&Mpidr, Entry + 68, sizeof (Mpidr));
      Log ("CPU_MADT_GICC uid=%u mpidr=0x%lx flags=0x%x gicr=0x%lx efficiency=%u\r\n",
           Uid, Mpidr, Flags, Gicr, Entry[76]);
      if (Uid >= 8 || (Present & (1U << Uid)) || Mpidr != ((UINT64)Uid << 8) || Flags > 1) {
        Log ("FAIL: MADT CPU identity invalid\r\n");
        return;
      }
      Present |= 1U << Uid;
      if (Flags & 1U) { Enabled |= 1U << Uid; Count++; }
    }
    Offset += Length;
  }
  Profile = Enabled == 1 ? "up" : Enabled == 3 ? "a55x2" : Enabled == 0x3f ? "a55x6" : Enabled == 0xff ? "smp" : NULL;
  if (Present != 0xff || Profile == NULL) { Log ("FAIL: MADT CPU profile invalid\r\n"); return; }
  Log ("CPU_MADT_EXPECTED profile=%a count=%u mask=0x%x\r\n", Profile, Count, Enabled);
}

STATIC VOID DumpAcpi (EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_ACPI_2_0_ROOT_SYSTEM_DESCRIPTION_POINTER *Rsdp = NULL;
  EFI_ACPI_DESCRIPTION_HEADER *Xsdt;
  UINTN Count;
  for (UINTN I = 0; I < SystemTable->NumberOfTableEntries; I++) {
    if (CompareGuid (&SystemTable->ConfigurationTable[I].VendorGuid, &gEfiAcpi20TableGuid)) {
      Rsdp = SystemTable->ConfigurationTable[I].VendorTable;
      break;
    }
  }
  if (Rsdp == NULL || Rsdp->Revision < 2 || Rsdp->XsdtAddress == 0) {
    Log ("FAIL: ACPI 2.0 XSDT missing\r\n");
    return;
  }
  Xsdt = (VOID *)(UINTN)Rsdp->XsdtAddress;
  if (Xsdt->Length < sizeof (*Xsdt) || Xsdt->Length > 4096) {
    Log ("FAIL: XSDT length invalid\r\n");
    return;
  }
  Count = (Xsdt->Length - sizeof (*Xsdt)) / sizeof (UINT64);
  for (UINTN I = 0; I < Count; I++) {
    UINT64 Address;
    EFI_ACPI_DESCRIPTION_HEADER *Table;
    UINT8 Sum = 0;
    CHAR8 Signature[5] = {0};
    CHAR16 Filename[48];
    EFI_FILE_PROTOCOL *File;
    CopyMem (&Address, (UINT8 *)Xsdt + sizeof (*Xsdt) + I * sizeof (Address), sizeof (Address));
    Table = (VOID *)(UINTN)Address;
    if (Table->Length < sizeof (*Table) || Table->Length > 1024 * 1024) {
      Log ("FAIL: ACPI[%u] invalid length\r\n", I);
      continue;
    }
    CopyMem (Signature, &Table->Signature, 4);
    for (UINTN J = 0; J < Table->Length; J++) Sum += ((UINT8 *)Table)[J];
    Log ("ACPI %a length=%u revision=%u checksum=%u address=0x%lx\r\n",
         Signature, Table->Length, Table->Revision, Sum, Address);
    if (Table->Signature == SIGNATURE_32 ('A', 'P', 'I', 'C') && Sum == 0) DumpCpuTopology (Table);
    UnicodeSPrint (Filename, sizeof (Filename), L"%a-%u.bin", Signature, I);
    if (mDir != NULL && !EFI_ERROR (OpenNew (mDir, Filename, &File))) {
      UINTN Size = Table->Length;
      File->Write (File, &Size, Table);
      File->Flush (File);
      File->Close (File);
    }
  }
}

STATIC VOID DumpMemory (VOID)
{
  UINTN Size = 0, Key = 0, DescriptorSize = 0;
  UINT32 Version = 0;
  EFI_MEMORY_DESCRIPTOR *Map = NULL;
  EFI_STATUS Status;
  UINT64 Usable = 0;
  Status = gBS->GetMemoryMap (&Size, Map, &Key, &DescriptorSize, &Version);
  if (Status != EFI_BUFFER_TOO_SMALL || DescriptorSize == 0) {
    Log ("GetMemoryMap(size): %r\r\n", Status);
    return;
  }
  Size += DescriptorSize * 32;
  Map = AllocatePool (Size);
  if (Map == NULL) return;
  Status = gBS->GetMemoryMap (&Size, Map, &Key, &DescriptorSize, &Version);
  Log ("GetMemoryMap: %r descriptor=%u version=%u\r\n", Status, DescriptorSize, Version);
  if (!EFI_ERROR (Status)) {
    for (UINTN Offset = 0; Offset < Size; Offset += DescriptorSize) {
      EFI_MEMORY_DESCRIPTOR *D = (VOID *)((UINT8 *)Map + Offset);
      Log ("MEM type=%u base=0x%lx pages=0x%lx attr=0x%lx\r\n",
           D->Type, D->PhysicalStart, D->NumberOfPages, D->Attribute);
      if (D->Type == EfiConventionalMemory) Usable += EFI_PAGES_TO_SIZE (D->NumberOfPages);
    }
    Log ("Conventional memory bytes=%lu (before bootmgr and WIM)\r\n", Usable);
  }
  FreePool (Map);
}

STATIC VOID DumpPsci (VOID)
{
  ARM_SMC_ARGS Args;
  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = 0x84000000; // PSCI_VERSION: read-only firmware query.
  ArmCallSmc (&Args);
  Log ("PSCI_VERSION=0x%lx\r\n", (UINT64)Args.Arg0);
  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = 0x8400000A; // PSCI_FEATURES(CPU_ON64).
  Args.Arg1 = 0xC4000003;
  ArmCallSmc (&Args);
  Log ("PSCI_CPU_ON64_FEATURES=0x%lx\r\n", (UINT64)Args.Arg0);
  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = 0x8400000A;
  Args.Arg1 = 0x84000003; // CPU_ON32 support, independently of CPU_ON64.
  ArmCallSmc (&Args);
  Log ("PSCI_CPU_ON32_FEATURES=0x%lx\r\n", (UINT64)Args.Arg0);
  for (UINTN Cpu = 0; Cpu < 8; Cpu++) {
    ZeroMem (&Args, sizeof (Args));
    Args.Arg0 = 0xC4000004; // AFFINITY_INFO64; never starts/stops a core.
    Args.Arg1 = Cpu << 8;
    ArmCallSmc (&Args);
    Log ("PSCI_AFFINITY_INFO mpidr=0x%lx state=0x%lx\r\n", (UINT64)(Cpu << 8), (UINT64)Args.Arg0);
  }
}

STATIC VOID DumpGicRedistributors (VOID)
{
  // Android's eight GICR frames are separated by 0x20000. Reading the
  // identification and power status registers does not wake a CPU or change
  // interrupt state. Log before each read to identify any firmware-side stall.
  Log ("SMP_GICD_TYPER=0x%x\r\n", MmioRead32 (0x0C000004));
  for (UINTN Cpu = 0; Cpu < 8; Cpu++) {
    UINTN Base = 0x0C040000 + Cpu * 0x20000;
    UINT64 Typer, Affinity;
    UINT32 Waker;
    Log ("SMP_GICR_READ_BEGIN index=%u base=0x%lx\r\n", (UINT32)Cpu, (UINT64)Base);
    Typer = MmioRead64 (Base + 8);
    Waker = MmioRead32 (Base + 0x14);
    Affinity = ((Typer >> 32) & 0x00FFFFFF) | (((Typer >> 56) & 0xFF) << 32);
    Log ("SMP_GICR index=%u base=0x%lx typer=0x%lx affinity=0x%lx last=%u waker=0x%x\r\n",
         (UINT32)Cpu, (UINT64)Base, Typer, Affinity, (UINT32)((Typer >> 4) & 1), Waker);
  }
}

EFI_STATUS EFIAPI LancelotPreflightEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  EFI_LOADED_IMAGE_PROTOCOL *Loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
  EFI_FILE_PROTOCOL *Root = NULL, *Woa = NULL, *Logs = NULL, *FirmwareLog = NULL;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_HANDLE WindowsImage;
  CHAR16 RunName[48];
  UINT64 BootId = 0, Cursor = 0, Start, RunId = 0;
  UINT64 CurrentEl, Mpidr, CounterFrequency, Sctlr;
  UINT8 Buffer[4096], SecureBoot = 0xff, SetupMode = 0xff;
  UINTN Size;

  Print (L"Lancelot WOA preflight: saving diagnostics before Windows...\r\n");
  if (mHandoff->Magic == LANCELOT_HANDOFF_MAGIC) {
    mHandoff->Stage = LANCELOT_STAGE_PREFLIGHT;
    BootId = mHandoff->BootId;
  }
  Status = gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Loaded);
  if (EFI_ERROR (Status)) return Status;
  Status = gBS->HandleProtocol (Loaded->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs);
  if (EFI_ERROR (Status)) return Status;
  Status = Fs->OpenVolume (Fs, &Root);
  if (EFI_ERROR (Status)) return Status;
  Status = Root->Open (Root, &Woa, L"WOA", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
  if (!EFI_ERROR (Status)) Status = Woa->Open (Woa, &Logs, L"logs", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
  gBS->GetNextMonotonicCount (&RunId);
  UnicodeSPrint (RunName, sizeof (RunName), L"%016lx-%016lx", BootId, RunId);
  if (!EFI_ERROR (Status)) Status = Logs->Open (Logs, &mDir, RunName, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY);
  if (!EFI_ERROR (Status)) Status = OpenNew (mDir, L"preflight.txt", &mLog);
  Log ("LANCELOT WOA PREFLIGHT v1 boot=%016lx file-open=%r\r\n", BootId, Status);
  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (CurrentEl));
  __asm__ volatile ("mrs %0, mpidr_el1" : "=r" (Mpidr));
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (CounterFrequency));
  __asm__ volatile ("mrs %0, sctlr_el1" : "=r" (Sctlr));
  Log ("ARM CurrentEL=0x%lx MPIDR=0x%lx CNTFRQ=%lu SCTLR_EL1=0x%lx\r\n",
       CurrentEl, Mpidr, CounterFrequency, Sctlr);
  DumpPsci ();
  DumpGicRedistributors ();
  Size = 1;
  Status = gRT->GetVariable (L"SecureBoot", &gEfiGlobalVariableGuid, NULL, &Size, &SecureBoot);
  Log ("SecureBoot=%u status=%r\r\n", SecureBoot, Status);
  Size = 1;
  Status = gRT->GetVariable (L"SetupMode", &gEfiGlobalVariableGuid, NULL, &Size, &SetupMode);
  Log ("SetupMode=%u status=%r\r\n", SetupMode, Status);
  Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (!EFI_ERROR (Status)) Log ("GOP %ux%u stride=%u format=%u base=0x%lx size=0x%lx\r\n",
    Gop->Mode->Info->HorizontalResolution, Gop->Mode->Info->VerticalResolution,
    Gop->Mode->Info->PixelsPerScanLine, Gop->Mode->Info->PixelFormat,
    Gop->Mode->FrameBufferBase, (UINT64)Gop->Mode->FrameBufferSize);
  else Log ("GOP missing: %r\r\n", Status);
  DumpAcpi (SystemTable);
  DumpMemory ();
  if (mDir != NULL && !EFI_ERROR (OpenNew (mDir, L"firmware-before-windows.log", &FirmwareLog))) {
    for (;;) {
      Size = BootLogSnapshot (Cursor, &BootId, &Start, Buffer, sizeof (Buffer));
      if (Size == 0) break;
      Cursor = Start + Size;
      FirmwareLog->Write (FirmwareLog, &Size, Buffer);
    }
    FirmwareLog->Flush (FirmwareLog);
    FirmwareLog->Close (FirmwareLog);
  }
  Path = FileDevicePath (Loaded->DeviceHandle, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
  if (Path == NULL) { Status = EFI_OUT_OF_RESOURCES; goto Done; }
  Log ("PHASE: LoadImage Windows Boot Manager\r\n");
  Status = gBS->LoadImage (FALSE, ImageHandle, Path, NULL, 0, &WindowsImage);
  FreePool (Path);
  Log ("LoadImage returned %r\r\n", Status);
  if (EFI_ERROR (Status)) goto Done;
  Log ("PHASE: StartImage next; subsequent logs use USB, then LancelotDiag.sys\r\n");
  if (mLog != NULL) { mLog->Flush (mLog); mLog->Close (mLog); mLog = NULL; }
  if (mHandoff->Magic == LANCELOT_HANDOFF_MAGIC) mHandoff->Stage = LANCELOT_STAGE_STARTIMAGE;
  Print (L"Starting Windows PE. Firmware logs are in \\WOA\\logs\\%s\\\r\n", RunName);
  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  Status = gBS->StartImage (WindowsImage, NULL, NULL);
  // This path only runs if Windows returns without exiting Boot Services.
  if (mDir != NULL) OpenNew (mDir, L"windows-returned.txt", &mLog);
  Log ("Windows StartImage returned %r\r\n", Status);
  if (mHandoff->Magic == LANCELOT_HANDOFF_MAGIC) mHandoff->LastStatus = (UINT32)Status;
Done:
  Print (L"Windows did not take control: %r. Use firmware USB log capture.\r\n", Status);
  if (mLog != NULL) { mLog->Flush (mLog); mLog->Close (mLog); }
  if (mDir != NULL) mDir->Close (mDir);
  if (Logs != NULL) Logs->Close (Logs);
  if (Woa != NULL) Woa->Close (Woa);
  if (Root != NULL) Root->Close (Root);
  // Keep the diagnostic transport alive for five minutes on failure.
  gBS->Stall (300000000);
  return Status;
}
