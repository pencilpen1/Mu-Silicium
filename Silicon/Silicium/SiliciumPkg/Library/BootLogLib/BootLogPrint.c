/** @file
  Bounded formatting for SEC diagnostics and boot-stage markers.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Library/BootLogLib.h>
#include <Library/PrintLib.h>

VOID EFIAPI BootLogVPrint (CONST CHAR8 *Format, VA_LIST Marker)
{
  CHAR8 Buffer[512];
  UINTN Length;
  if (Format == NULL) return;
  Length = AsciiVSPrint (Buffer, sizeof (Buffer), Format, Marker);
  BootLogWrite (Buffer, Length);
}

VOID EFIAPI BootLogBPrint (CONST CHAR8 *Format, BASE_LIST Marker)
{
  CHAR8 Buffer[512];
  UINTN Length;
  if (Format == NULL) return;
  Length = AsciiBSPrint (Buffer, sizeof (Buffer), Format, Marker);
  BootLogWrite (Buffer, Length);
}

VOID EFIAPI BootLogPrint (CONST CHAR8 *Format, ...)
{
  VA_LIST Marker;
  VA_START (Marker, Format);
  BootLogVPrint (Format, Marker);
  VA_END (Marker);
}
