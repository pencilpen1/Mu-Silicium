/** @file
  lancelot USB keyboard console path for the Host profile.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Library/DeviceBootManagerLib.h>
#include <Library/UefiBootManagerLib.h>

#pragma pack (1)
STATIC struct {
  USB_CLASS_DEVICE_PATH Keyboard;
  EFI_DEVICE_PATH_PROTOCOL End;
} mUsbKeyboard = {
  {{MESSAGING_DEVICE_PATH, MSG_USB_CLASS_DP, {sizeof (USB_CLASS_DEVICE_PATH), 0}},
   0xFFFF, 0xFFFF, 3, 1, 1},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {sizeof (EFI_DEVICE_PATH_PROTOCOL), 0}}
};
#pragma pack ()

EFI_HANDLE EFIAPI
DeviceBootManagerBeforeConsole (EFI_DEVICE_PATH_PROTOCOL **DevicePath,
                               BDS_CONSOLE_CONNECT_ENTRY **PlatformConsoles)
{
  if (DevicePath) *DevicePath = NULL;
  if (PlatformConsoles) *PlatformConsoles = NULL;
  EfiBootManagerUpdateConsoleVariable (ConIn, (EFI_DEVICE_PATH_PROTOCOL *)&mUsbKeyboard, NULL);
  return NULL;
}

EFI_DEVICE_PATH_PROTOCOL **EFIAPI DeviceBootManagerAfterConsole (VOID)
{
  EfiBootManagerConnectAll ();
  EfiBootManagerRefreshAllBootOption ();
  return NULL;
}

EFI_DEVICE_PATH_PROTOCOL **EFIAPI DeviceBootManagerOnDemandConInConnect (VOID) { return NULL; }
VOID EFIAPI DeviceBootManagerProcessBootCompletion (EFI_BOOT_MANAGER_LOAD_OPTION *Option) { }
EFI_STATUS EFIAPI DeviceBootManagerPriorityBoot (EFI_BOOT_MANAGER_LOAD_OPTION *Option) { return EFI_SUCCESS; }
VOID EFIAPI DeviceBootManagerUnableToBoot (VOID) { }
VOID EFIAPI DeviceBootManagerBdsEntry (VOID) { }
