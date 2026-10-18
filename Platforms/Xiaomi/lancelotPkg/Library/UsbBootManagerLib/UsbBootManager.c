/** @file
  lancelot USB keyboard console path for the Host profile.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#include <Library/DeviceBootManagerLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

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
STATIC
VOID
ShowUsbStandby (VOID)
{
  Print (L"\r\nLancelot USB standby - no bootable OS found.\r\n"
         L"USB Host / PC fastboot remain active.\r\n"
         L"F1: Boot Manager   F2: Rescan devices\r\n"
         L"Press keys to test console input; USB status is shown below.\r\n");
}

VOID EFIAPI
DeviceBootManagerUnableToBoot (VOID)
{
  EFI_EVENT Timer = NULL;
  EFI_STATUS Status;
  EFI_INPUT_KEY Key;
  UINTN Index;
  // The shared policy shuts down after ten seconds when this hook returns.
  // For this lancelot USB profile, stay at TPL_APPLICATION instead: timer
  // callbacks, keyboard parsing and role handoff must remain serviceable when
  // there is no installed OS. BootShim and the successful boot path are untouched.
  gBS->SetWatchdogTimer (0, 0, 0, NULL);
  Status = gBS->CreateEvent (EVT_TIMER, TPL_APPLICATION, NULL, NULL, &Timer);
  if (!EFI_ERROR (Status)) {
    Status = gBS->SetTimer (Timer, TimerPeriodic, 1000000); // 100 ms
    if (EFI_ERROR (Status)) { gBS->CloseEvent (Timer); Timer = NULL; }
  }
  ShowUsbStandby ();
  for (;;) {
    if (Timer == NULL || EFI_ERROR (gBS->WaitForEvent (1, &Timer, &Index)))
      gBS->Stall (100000);
    // Bounded drain so held/repeating keys do not starve timer dispatch.
    for (UINTN Count = 0; Count < 16 && gST->ConIn != NULL; Count++) {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (EFI_ERROR (Status)) break;
      if (Key.ScanCode == SCAN_F1) {
        EFI_BOOT_MANAGER_LOAD_OPTION Menu;
        Status = EfiBootManagerGetBootManagerMenu (&Menu);
        if (!EFI_ERROR (Status)) {
          EfiBootManagerBoot (&Menu);
          EfiBootManagerFreeLoadOption (&Menu);
          gBS->SetWatchdogTimer (0, 0, 0, NULL);
        } else Print (L"Boot Manager: %r\r\n", Status);
        ShowUsbStandby ();
      } else if (Key.ScanCode == SCAN_F2) {
        EfiBootManagerConnectAll ();
        EfiBootManagerRefreshAllBootOption ();
        Print (L"USB/storage rescan complete. F1 opens Boot Manager.\r\n");
      } else {
        Print (L"Key received: scan=%04x char=%04x\r\n", Key.ScanCode, Key.UnicodeChar);
      }
    }
  }
}
VOID EFIAPI DeviceBootManagerBdsEntry (VOID) { }
