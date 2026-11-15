/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BootLogLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include "lancelot_handoff.h"
#include "lancelot_board.h"
#include "lancelot_charger_handoff.h"

STATIC volatile LANCELOT_HANDOFF *mHandoff = (VOID *)(UINTN)LANCELOT_HANDOFF_BASE;
STATIC EFI_EVENT mReadyEvent;
STATIC EFI_EVENT mExitEvent;

STATIC VOID EFIAPI Ready (EFI_EVENT Event, VOID *Context)
{
  LancelotChargerPrepare ();
  mHandoff->Stage = LANCELOT_STAGE_READY;
  if (BootLogUsbActive ()) mHandoff->Flags |= LANCELOT_FLAG_USB_INITIALIZED;
  BootLogPrint ("[WOA] READY_TO_BOOT USB initialized=%u, handoff=0x%lx\n",
                (mHandoff->Flags & LANCELOT_FLAG_USB_INITIALIZED) != 0,
                (UINT64)LANCELOT_HANDOFF_BASE);
}

STATIC VOID EFIAPI ExitBoot (EFI_EVENT Event, VOID *Context)
{
  // No allocations, filesystem calls, timers, or firmware-table changes here.
  LancelotChargerExit ();
  if (!LancelotKeysReady (MmioRead32)) mHandoff->Flags &= ~LANCELOT_FLAG_KEYS_READY;
  if (!LancelotBatteryReady (MmioRead32)) mHandoff->Flags &= ~LANCELOT_FLAG_BATTERY_READY;
  if (!LancelotTouchReady (MmioRead32) ||
      (MmioRead32 (LBOARD_SPI_ENABLE) & (1U << 10)) != 0) mHandoff->Flags &= ~LANCELOT_FLAG_TOUCH_READY;
  BootLogPrint ("[WOA] BOARD_EBS flags=0x%x gpio93_mode=0x%x dir=0x%x wacs2=0x%x spi_clock=0x%x gate=0x%x spi_irq=0x%x\n",
                mHandoff->Flags, MmioRead32 (LBOARD_GPIO_MODE93), MmioRead32 (LBOARD_GPIO_DIR93),
                MmioRead32 (0x1000DC24U), MmioRead32 (LBOARD_SPI_CLOCK),
                MmioRead32 (LBOARD_SPI_GATE), MmioRead32 (LBOARD_SPI_ENABLE));
  mHandoff->Stage = LANCELOT_STAGE_EBS;
  mHandoff->Flags |= LANCELOT_FLAG_EBS;
  BootLogPrint ("[WOA] EXIT_BOOT_SERVICES: Windows owns hardware next\n");
  WriteBackDataCacheRange ((VOID *)(UINTN)LANCELOT_FW_LOG_BASE, LANCELOT_FW_LOG_SIZE);
  WriteBackDataCacheRange ((VOID *)(UINTN)LANCELOT_HANDOFF_BASE, LANCELOT_HANDOFF_SIZE);
}

EFI_STATUS EFIAPI LancelotDiagEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  UINT64 BootId, Cursor;
  UINT8 Byte;
  STATIC_ASSERT (sizeof (LANCELOT_HANDOFF) == 128, "handoff ABI");
  ZeroMem ((VOID *)mHandoff, sizeof (*mHandoff));
  LancelotChargerInitialize ();
  BootLogSnapshot (0, &BootId, &Cursor, &Byte, sizeof (Byte));
  mHandoff->Version = LANCELOT_HANDOFF_VERSION;
  mHandoff->Size = sizeof (*mHandoff);
  mHandoff->BootId = BootId;
  mHandoff->Stage = LANCELOT_STAGE_DXE;
  mHandoff->FirmwareLogBase = LANCELOT_FW_LOG_BASE;
  mHandoff->FirmwareLogSize = LANCELOT_FW_LOG_SIZE;
  mHandoff->UsbBase = LANCELOT_USB_BASE;
  mHandoff->UsbSize = LANCELOT_USB_SIZE;
  mHandoff->UsbGsi = LANCELOT_USB_GSI;
  mHandoff->UartBase = LANCELOT_UART_BASE;
  mHandoff->UartBaud = LANCELOT_UART_BAUD;
  mHandoff->FrameBufferBase = 0x7cdb0000;
  mHandoff->FrameBufferSize = 0x2250000;
  mHandoff->Width = 1080;
  mHandoff->Height = 2340;
  mHandoff->Stride = 1088;
  mHandoff->CounterFrequency = 13000000;
  MemoryFence ();
  mHandoff->Magic = LANCELOT_HANDOFF_MAGIC;
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, Ready, NULL,
                             &gEfiEventReadyToBootGuid, &mReadyEvent);
  if (EFI_ERROR (Status)) return Status;
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, ExitBoot, NULL,
                             &gEfiEventExitBootServicesGuid, &mExitEvent);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (mReadyEvent);
    return Status;
  }
  BootLogPrint ("[WOA] DXE diagnostic handoff installed, boot=%016lx\n", BootId);
  return EFI_SUCCESS;
}
