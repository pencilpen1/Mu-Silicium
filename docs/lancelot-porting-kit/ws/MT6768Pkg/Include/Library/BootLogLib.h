/** @file
  Single-CPU, non-blocking boot log shared by all firmware images.
  No Boot Services, allocation, USB or text-console calls on the write path.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef LANCELOT_BOOT_LOG_LIB_H
#define LANCELOT_BOOT_LOG_LIB_H

#include <Base.h>

// Called once at the SEC C entry. All other images only attach to this buffer.
VOID EFIAPI BootLogInitialize (UINT64 BootId);
VOID EFIAPI BootLogWrite (CONST VOID *Buffer, UINTN Length);
VOID EFIAPI BootLogPrint (CONST CHAR8 *Format, ...);
VOID EFIAPI BootLogVPrint (CONST CHAR8 *Format, VA_LIST Marker);
VOID EFIAPI BootLogBPrint (CONST CHAR8 *Format, BASE_LIST Marker);

// Returns a consistent snapshot, clamping old/future cursors to retained data.
// Start is the actual first byte sequence; next cursor is Start + return value.
UINTN EFIAPI BootLogSnapshot (
  UINT64 Requested, UINT64 *BootId, UINT64 *Start, VOID *Buffer, UINTN Size);

// Avoid slow framebuffer DEBUG rendering once USB starts. GOP is independent.
VOID EFIAPI BootLogSetUsbActive (BOOLEAN Active);
BOOLEAN EFIAPI BootLogUsbActive (VOID);

#endif
