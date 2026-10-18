/** @file
  Fixed RAM ring: usable before MMU/HOBs and shared across SEC/DXE images.
  Only the boot CPU runs. IRQ masking serializes writers and USB snapshots;
  a shared busy flag drops recursive fault output instead of spinning.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Library/ArmLib.h>
#include <Library/BaseLib.h>
#include <Library/BootLogLib.h>
#include <Configuration/BootLogConfig.h>

#define LOG_MAGIC  0x474f4c4cU
#define LOG_VERSION  1U
#define LOG_CAPACITY  (LANCELOT_LOG_SIZE - 32U)

typedef struct {
  UINT32 Magic;
  UINT32 Version;
  UINT64 BootId;
  UINT64 Written;
  UINT32 Busy;
  UINT32 UsbActive;
  UINT8 Data[LOG_CAPACITY];
} BOOT_LOG;

STATIC_ASSERT (sizeof (BOOT_LOG) == LANCELOT_LOG_SIZE, "boot log layout");

// Host tests substitute ordinary RAM; firmware uses the fixed reserved range.
#ifndef BOOT_LOG_ADDRESS
#define BOOT_LOG_ADDRESS  LANCELOT_LOG_BASE
#endif
#define LOG  ((volatile BOOT_LOG *)(UINTN)BOOT_LOG_ADDRESS)

STATIC BOOLEAN Valid (VOID)
{
  return (LOG->Magic == LOG_MAGIC) && (LOG->Version == LOG_VERSION);
}

VOID EFIAPI BootLogInitialize (UINT64 BootId)
{
  BOOLEAN Irq = ArmGetInterruptState ();
  ArmDisableInterrupts ();
  LOG->Magic = 0;
  LOG->Version = LOG_VERSION;
  LOG->BootId = BootId;
  LOG->Written = 0;
  LOG->Busy = 0;
  LOG->UsbActive = 0;
  MemoryFence ();
  LOG->Magic = LOG_MAGIC;
  if (Irq) ArmEnableInterrupts ();
}

VOID EFIAPI BootLogWrite (CONST VOID *Buffer, UINTN Length)
{
  CONST UINT8 *Bytes = Buffer;
  BOOLEAN Irq;
  UINT64 End;
  UINTN Index;

  if ((Buffer == NULL) || (Length == 0)) return;
  Irq = ArmGetInterruptState ();
  ArmDisableInterrupts ();
  if (!Valid () || LOG->Busy) goto Done;
  End = LOG->Written;
  if (Length > MAX_UINT64 - End) goto Done;
  LOG->Busy = 1;
  // Oversized writes retain their tail without an unbounded copy.
  if (Length > LOG_CAPACITY) {
    UINTN Skip = Length - LOG_CAPACITY;
    Bytes += Skip;
    End += Skip;
    Length = LOG_CAPACITY;
  }
  Index = (UINTN)(End % LOG_CAPACITY);
  for (UINTN I = 0; I < Length; I++) {
    LOG->Data[Index++] = Bytes[I];
    if (Index == LOG_CAPACITY) Index = 0;
  }
  MemoryFence ();
  LOG->Written = End + Length;
  LOG->Busy = 0;
Done:
  if (Irq) ArmEnableInterrupts ();
}

UINTN EFIAPI BootLogSnapshot (
  UINT64 Requested, UINT64 *BootId, UINT64 *Start, VOID *Buffer, UINTN Size)
{
  BOOLEAN Irq = ArmGetInterruptState ();
  UINT64 End, Oldest;
  UINTN Count = 0, Index;
  UINT8 *Bytes = Buffer;

  if ((BootId == NULL) || (Start == NULL) || (Buffer == NULL)) return 0;
  *BootId = *Start = 0;
  ArmDisableInterrupts ();
  if (!Valid () || LOG->Busy) goto Done;
  LOG->Busy = 1;
  End = LOG->Written;
  Oldest = End > LOG_CAPACITY ? End - LOG_CAPACITY : 0;
  if ((Requested < Oldest) || (Requested > End)) Requested = Oldest;
  *BootId = LOG->BootId;
  *Start = Requested;
  Count = (UINTN)MIN ((UINT64)Size, End - Requested);
  Index = (UINTN)(Requested % LOG_CAPACITY);
  for (UINTN I = 0; I < Count; I++) {
    Bytes[I] = LOG->Data[Index++];
    if (Index == LOG_CAPACITY) Index = 0;
  }
  LOG->Busy = 0;
Done:
  if (Irq) ArmEnableInterrupts ();
  return Count;
}

VOID EFIAPI BootLogSetUsbActive (BOOLEAN Active)
{
  if (Valid ()) LOG->UsbActive = Active;
}

BOOLEAN EFIAPI BootLogUsbActive (VOID)
{
  return Valid () && (LOG->UsbActive != 0);
}
