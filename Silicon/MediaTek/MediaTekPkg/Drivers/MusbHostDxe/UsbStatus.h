/** @file
  In-place status text, retaining the framebuffer underneath the glyphs.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef LANCELOT_USB_STATUS_H
#define LANCELOT_USB_STATUS_H
#include <Uefi.h>

// Failure to allocate display state is nonfatal to USB initialization.
VOID UsbStatusInitialize (VOID);
VOID UsbStatusUpdate (CONST CHAR8 *State, CONST CHAR8 *Detail);
#endif
