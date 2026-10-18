/** @file
  SEC RAM-only debug output with visible, fatal assertions (no SerialPortLib).
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/BootScreen.h>
#include <Library/DebugPrintErrorLevelLib.h>

VOID EFIAPI DebugVPrint (UINTN ErrorLevel, CONST CHAR8 *Format, VA_LIST Marker)
{
  if ((ErrorLevel & GetDebugPrintErrorLevel ()) != 0) BootLogVPrint (Format, Marker);
}
VOID EFIAPI DebugBPrint (UINTN ErrorLevel, CONST CHAR8 *Format, BASE_LIST Marker)
{
  if ((ErrorLevel & GetDebugPrintErrorLevel ()) != 0) BootLogBPrint (Format, Marker);
}
VOID EFIAPI DebugPrint (UINTN ErrorLevel, CONST CHAR8 *Format, ...)
{
  VA_LIST Marker;
  VA_START (Marker, Format);
  DebugVPrint (ErrorLevel, Format, Marker);
  VA_END (Marker);
}

VOID
EFIAPI
DebugAssert (CONST CHAR8 *FileName, UINTN LineNumber, CONST CHAR8 *Description)
{
  STATIC volatile BOOLEAN InAssert;
  CONST CHAR8 *BaseName = FileName;

  if (InAssert) {
    return;
  }
  InAssert = TRUE;
  BootLogPrint ("SEC ASSERT %a(%lu): %a\n", FileName != NULL ? FileName : "?",
                (UINT64)LineNumber, Description != NULL ? Description : "?");
  if (FileName != NULL) {
    for (CONST CHAR8 *P = FileName; *P != 0; P++) {
      if ((*P == '/') || (*P == '\\')) {
        BaseName = P + 1;
      }
    }
  }
  BootScreenHex (128, "ASSERT LINE=", LineNumber, 0xff500000U);
  BootScreenText (176, BaseName != NULL ? BaseName : "?", 0xff500000U);
  BootScreenText (224, Description != NULL ? Description : "?", 0xff500000U);
  CpuDeadLoop ();
}

VOID *EFIAPI DebugClearMemory (VOID *Buffer, UINTN Length) { return Buffer; }
BOOLEAN EFIAPI DebugAssertEnabled (VOID) { return TRUE; }
BOOLEAN EFIAPI DebugPrintEnabled (VOID) { return TRUE; }
BOOLEAN EFIAPI DebugCodeEnabled (VOID) { return TRUE; }
BOOLEAN EFIAPI DebugClearMemoryEnabled (VOID) { return FALSE; }
BOOLEAN EFIAPI DebugPrintLevelEnabled (CONST UINTN ErrorLevel)
{
  return (ErrorLevel & GetDebugPrintErrorLevel ()) != 0;
}
