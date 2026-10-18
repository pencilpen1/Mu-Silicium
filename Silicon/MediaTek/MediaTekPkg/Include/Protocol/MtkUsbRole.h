/** @file
  Single-owner handoff for the lancelot USB MAC/PHY. Callbacks run at TPL_NOTIFY.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef MTK_USB_ROLE_H
#define MTK_USB_ROLE_H
#include <Uefi.h>

typedef enum { MtkUsbRoleNone, MtkUsbRoleDevice, MtkUsbRoleHost } MTK_USB_ROLE;
typedef EFI_STATUS (EFIAPI *MTK_USB_DEVICE_START)(VOID *Context);
typedef VOID (EFIAPI *MTK_USB_DEVICE_STOP)(VOID *Context);
typedef struct _MTK_USB_ROLE_PROTOCOL MTK_USB_ROLE_PROTOCOL;

struct _MTK_USB_ROLE_PROTOCOL {
  // Registers a resident device driver. Hardware is started/stopped later by
  // the role manager; registration itself does not touch the controller.
  EFI_STATUS (EFIAPI *RegisterDevice)(MTK_USB_ROLE_PROTOCOL *This,
      MTK_USB_DEVICE_START Start, MTK_USB_DEVICE_STOP Stop, VOID *Context);
};
extern EFI_GUID gMediaTekUsbRoleProtocolGuid;
#endif
