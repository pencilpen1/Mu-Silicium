/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BootLogLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/MtkPmicWrapperHandoff.h>
#include "lancelot_handoff.h"
#include "lancelot_board.h"
#include "LancelotBatteryHandoff.h"
#include "lancelot_charger_handoff.h"
#include "LancelotGpuSource.h"

STATIC volatile LANCELOT_HANDOFF *mHandoff = (VOID *)(UINTN)LANCELOT_HANDOFF_BASE;
STATIC EFI_EVENT mReadyEvent;
STATIC EFI_EVENT mBeforeExitEvent;
STATIC EFI_EVENT mExitEvent;
STATIC EFI_STATUS mBatteryClose = EFI_NOT_READY;
STATIC UINT64 mBatteryFrequency;
STATIC BATTERY_OBSERVATION mBattery;

STATIC VOID EFIAPI BeforeExitBoot (EFI_EVENT Event, VOID *Context)
{
  // This boot-services callback runs after Windows has finished loading its
  // files. Closing at ReadyToBoot could disrupt UEFI storage or keypad calls.
  if (mHandoff->Stage != LANCELOT_STAGE_STARTIMAGE) return;
  MTK_PMIC_WRAPPER_HANDOFF_PROTOCOL *Wrapper;
  mBatteryClose = gBS->LocateProtocol (&gMediaTekPmicWrapperHandoffProtocolGuid, NULL, (VOID **)&Wrapper);
  if (!EFI_ERROR (mBatteryClose)) mBatteryClose = Wrapper->CloseNormalAp ();
  mBatteryFrequency = GetPerformanceCounterProperties (NULL, NULL);
  BootLogPrint ("[WOA] PWRAP_CUTOFF phase=BEFORE_EBS boot=%016lx normal_ap_status=%r counter_hz=%Lu secure_exclusive=0\n",
                mHandoff->BootId, mBatteryClose, mBatteryFrequency);
}

STATIC VOID EFIAPI Ready (EFI_EVENT Event, VOID *Context)
{
  BATTERY_SAMPLE Sample;
  BatteryReadSample (MmioRead32, GetPerformanceCounter, &Sample);
  BootLogPrint ("[WOA] PWRAP_OBSERVE phase=READY boot=%016lx ticks=%Lu en=0x%x init_done=0x%x rdata=0x%x observation_only=1\n",
                mHandoff->BootId, Sample.Completed, Sample.Enable, Sample.Init, Sample.State);
  LancelotGpuSourceReady ();
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
  LancelotGpuSourceExit ();
  LancelotChargerExit ();
  if (!LancelotKeysReady (MmioRead32)) mHandoff->Flags &= ~LANCELOT_FLAG_KEYS_READY;
  UINT32 BatteryFlagsBefore = mHandoff->Flags;
  BATTERY_TERMINAL BatteryResult = BatteryObserve (mBatteryClose == EFI_SUCCESS,
                                      mBatteryFrequency, MmioRead32, GetPerformanceCounter, &mBattery);
  BOOLEAN BatteryReady = BatteryResult == BatteryStable;
  if (BatteryReady) mHandoff->Flags |= LANCELOT_FLAG_BATTERY_READY;
  else mHandoff->Flags &= ~LANCELOT_FLAG_BATTERY_READY;
  MemoryFence ();
  if (!LancelotTouchReady (MmioRead32) ||
      (MmioRead32 (LBOARD_SPI_ENABLE) & (1U << 10)) != 0) mHandoff->Flags &= ~LANCELOT_FLAG_TOUCH_READY;
  BootLogPrint ("[WOA] BOARD_EBS flags=0x%x gpio93_mode=0x%x dir=0x%x wacs2=0x%x spi_clock=0x%x gate=0x%x spi_irq=0x%x\n",
                mHandoff->Flags, MmioRead32 (LBOARD_GPIO_MODE93), MmioRead32 (LBOARD_GPIO_DIR93),
                mBattery.Count ? mBattery.Samples[mBattery.Count - 1].State : 0, MmioRead32 (LBOARD_SPI_CLOCK),
                MmioRead32 (LBOARD_SPI_GATE), MmioRead32 (LBOARD_SPI_ENABLE));
  BootLogPrint ("[WOA] EBS_BATTERY_CHECK flags_before=0x%x flags_after=0x%x ready=%u\n",
                BatteryFlagsBefore, mHandoff->Flags, BatteryReady);
  BootLogPrint ("[WOA] PWRAP_HANDOFF phase=EBS boot=%016lx normal_ap_closed=%u secure_exclusive=0 count=%u streak=%u reason=%u elapsed_ticks=%Lu\n",
                mHandoff->BootId, mBatteryClose == EFI_SUCCESS, mBattery.Count,
                mBattery.Streak, BatteryResult, mBattery.Final - mBattery.Start);
  for (UINT32 I = 0; I < mBattery.Count; ++I) {
    CONST BATTERY_SAMPLE *S = &mBattery.Samples[I];
    BootLogPrint ("[WOA] PWRAP_SAMPLE phase=EBS boot=%016lx index=%u ticks=%Lu valid_mask=0x7 en=0x%x init_done=0x%x rdata=0x%x\n",
                  mHandoff->BootId, I, S->Completed, S->Enable, S->Init, S->State);
  }
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
  LancelotGpuSourceInitialize (ImageHandle, BootId);
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
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_CALLBACK, BeforeExitBoot, NULL,
                              &gEfiEventBeforeExitBootServicesGuid, &mBeforeExitEvent);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (mReadyEvent);
    return Status;
  }
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, ExitBoot, NULL,
                             &gEfiEventExitBootServicesGuid, &mExitEvent);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (mBeforeExitEvent);
    gBS->CloseEvent (mReadyEvent);
    return Status;
  }
  BootLogPrint ("[WOA] DXE diagnostic handoff installed, boot=%016lx\n", BootId);
  return EFI_SUCCESS;
}
