/** @file
  Temporary lancelot bring-up display, independent of SerialPortLib/GOP.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef MT6768_BOOT_SCREEN_H
#define MT6768_BOOT_SCREEN_H

#include <Base.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/BootLogLib.h>
#include <Resources/font5x12.h>

// Matches the validated lancelot framebuffer PCDs. Reserve rows 0..383
// in the text console for these diagnostics. S and D never overwrite each other.
#define BOOT_SCREEN_BASE    0x7cdb0000U
#define BOOT_SCREEN_STRIDE  1088U
#define BOOT_SCREEN_ROWS    384U

STATIC inline
VOID
BootScreenFlush (UINTN Y, UINTN Rows)
{
  UINTN Control;

  // SEC now runs at EL1. With the MMU disabled data is not cacheable;
  // avoid VA cache-maintenance instructions on the earliest fault path.
  __asm__ __volatile__ ("mrs %0, sctlr_el1" : "=r" (Control));
  if ((Control & 1) != 0) {
    WriteBackDataCacheRange (
      (VOID *)(UINTN)(BOOT_SCREEN_BASE + Y * BOOT_SCREEN_STRIDE * 4),
      Rows * BOOT_SCREEN_STRIDE * 4);
  }
  __asm__ __volatile__ ("dsb sy" ::: "memory");
}

STATIC inline
VOID
BootScreenText (UINTN Y, CONST CHAR8 *Text, UINT32 Color)
{
  volatile UINT32 *Fb = (volatile UINT32 *)(UINTN)BOOT_SCREEN_BASE;
  UINTN Column = 0;

  for (UINTN Row = Y; Row < Y + 48; Row++) {
    for (UINTN X = 0; X < 1080; X++) {
      Fb[Row * BOOT_SCREEN_STRIDE + X] = Color;
    }
  }
  while ((*Text != 0) && (Column < 43)) {
    UINT8 C = (UINT8)*Text++;
    if ((C < 32) || (C >= 127)) {
      C = '?';
    }
    for (UINTN Row = 0; Row < 12; Row++) {
      UINT32 Bits = font5x12[(C - 32) * 2 + Row / 6] >> ((Row % 6) * 5);
      for (UINTN X = 0; X < 5; X++) {
        for (UINTN Dy = 0; Dy < 4; Dy++) {
          for (UINTN Dx = 0; Dx < 4; Dx++) {
            Fb[(Y + Row * 4 + Dy) * BOOT_SCREEN_STRIDE +
               Column * 24 + X * 4 + Dx] = (Bits & (1U << X)) ? 0xffffffffU : Color;
          }
        }
      }
    }
    Column++;
  }
  BootScreenFlush (Y, 48);
}

STATIC inline
VOID
BootScreenHex (UINTN Y, CONST CHAR8 *Name, UINT64 Value, UINT32 Color)
{
  CHAR8 Text[32];
  CONST CHAR8 *Hex = "0123456789ABCDEF";
  UINTN N = 0;

  while ((*Name != 0) && (N < 12)) {
    Text[N++] = *Name++;
  }
  for (UINTN I = 0; I < 16; I++) {
    Text[N++] = Hex[(Value >> (60 - I * 4)) & 15];
  }
  Text[N] = 0;
  // USB telemetry is repainted every second; do not feed it back into the log.
  if (Y < BOOT_SCREEN_ROWS) BootLogPrint ("%a\n", Text);
  BootScreenText (Y, Text, Color);
}

STATIC inline
VOID
BootScreenStage (CHAR8 Phase, UINT8 Stage)
{
  CONST CHAR8 *Hex = "0123456789ABCDEF";
  CHAR8 Text[] = { Phase, Hex[Stage >> 4], Hex[Stage & 15], 0 };

  BootLogPrint ("[STAGE] %a\n", Text);
  BootScreenText (Phase == 'S' ? 0 : 64, Text,
                  Phase == 'S' ? 0xff005000U : 0xff500050U);
}
#endif
