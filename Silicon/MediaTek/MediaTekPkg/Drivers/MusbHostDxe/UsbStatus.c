/** @file
  lancelot USB status overlay. Never clear a row or paint a background rectangle.
  Restore only pixels still owned by a previous glyph; adopt newer UI pixels as
  the background instead of restoring a stale screenshot over GOP/console output.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include "UsbStatus.h"
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Resources/font5x12.h>

#define STATUS_COLUMNS 43U
#define STATUS_SCALE   4U
#define STATUS_CELL_W  24U
#define STATUS_CELL_H  48U
#define STATUS_WIDTH   (STATUS_COLUMNS * STATUS_CELL_W)
#define STATUS_HEIGHT  (2 * STATUS_CELL_H)
#define STATUS_TOP     2244U
#define STATUS_STRIDE  1088U
#ifndef USB_STATUS_ADDRESS
#define USB_STATUS_ADDRESS 0x7CDB0000U
#endif

typedef struct {
  UINT32 Background[STATUS_HEIGHT][STATUS_WIDTH];
  BOOLEAN Painted[STATUS_HEIGHT][STATUS_WIDTH];
} STATUS_SURFACE;

STATIC STATUS_SURFACE *mSurface;

VOID
UsbStatusInitialize (VOID)
{
  if (mSurface == NULL) mSurface = AllocateZeroPool (sizeof (*mSurface));
}

STATIC
UINT32
Ink (UINT32 Background)
{
  UINT32 Brightness = (Background & 255) + ((Background >> 8) & 255) + ((Background >> 16) & 255);
  return Brightness > 384 ? 0xFF000000U : 0xFFFFFFFFU;
}

STATIC
VOID
UpdateLine (UINTN Line, CONST CHAR8 *Text)
{
  volatile UINT32 *Fb = (volatile UINT32 *)(UINTN)USB_STATUS_ADDRESS;
  UINT8 Characters[STATUS_COLUMNS];
  BOOLEAN End = Text == NULL;
  UINTN DirtyStart[STATUS_CELL_H], DirtyEnd[STATUS_CELL_H];
  for (UINTN Row = 0; Row < STATUS_CELL_H; Row++) {
    DirtyStart[Row] = STATUS_WIDTH;
    DirtyEnd[Row] = 0;
  }
  for (UINTN Column = 0; Column < STATUS_COLUMNS; Column++) {
    UINT8 C = End ? 0 : (UINT8)*Text++;
    if (C == 0) End = TRUE;
    Characters[Column] = End ? ' ' : (C < 32 || C >= 127) ? '?' : C;
  }
  for (UINTN Row = 0; Row < STATUS_CELL_H; Row++) {
    UINTN GlyphRow = Row / STATUS_SCALE;
    UINTN Y = Line * STATUS_CELL_H + Row;
    for (UINTN Column = 0; Column < STATUS_COLUMNS; Column++) {
      UINT32 Bits = font5x12[(Characters[Column] - 32) * 2 + GlyphRow / 6] >> ((GlyphRow % 6) * 5);
      for (UINTN Dx = 0; Dx < STATUS_CELL_W; Dx++) {
        UINTN X = Column * STATUS_CELL_W + Dx;
        UINTN Offset = (STATUS_TOP + Y) * STATUS_STRIDE + X;
        UINT32 Current = Fb[Offset];
        UINT32 Background = Current;
        BOOLEAN Draw = Dx < 20 && (Bits & (1U << (Dx / STATUS_SCALE))) != 0;
        if (mSurface->Painted[Y][X] && Current == Ink (mSurface->Background[Y][X]))
          Background = mSurface->Background[Y][X];
        // If another UI writer changed this pixel, Current is the new base.
        // Unchanged glyph pixels are not written again and do not need a flush.
        UINT32 Desired = Draw ? Ink (Background) : Background;
        mSurface->Background[Y][X] = Background;
        mSurface->Painted[Y][X] = Draw;
        if (Current != Desired) {
          Fb[Offset] = Desired;
          if (DirtyStart[Row] > X) DirtyStart[Row] = X;
          DirtyEnd[Row] = X + 1;
        }
      }
    }
  }
  for (UINTN Row = 0; Row < STATUS_CELL_H; Row++) {
    if (DirtyEnd[Row] != 0) {
      UINTN Offset = (STATUS_TOP + Line * STATUS_CELL_H + Row) * STATUS_STRIDE + DirtyStart[Row];
      WriteBackDataCacheRange ((VOID *)(UINTN)&Fb[Offset],
                              (DirtyEnd[Row] - DirtyStart[Row]) * sizeof (UINT32));
    }
  }
}

VOID
UsbStatusUpdate (CONST CHAR8 *State, CONST CHAR8 *Detail)
{
  if (mSurface == NULL) return;
  UpdateLine (0, State);
  UpdateLine (1, Detail);
}
