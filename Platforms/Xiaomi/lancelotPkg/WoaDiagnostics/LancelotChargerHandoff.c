/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Additive I2C7 polling handoff. No SMB1351 transactions or charge writes.
 * Only the diagnostic CCFG bits can be asserted by this firmware version.
 */
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BootLogLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/MtkClock.h>
#include "lancelot_charger_handoff.h"

#define CHG_VERSION 0x00010000U
#define CHG_DIAGNOSTIC 0x31U
#define I2C7 0x11004000U
#define I2C7_DMA 0x11000680U
#define I2C_GATE 0x10001090U
#define I2C_MUX 0x100000A0U
#define MODE175 0x10005450U
#define MODE176 0x10005460U
#define GIC_ENABLE 0x0C000110U /* INTID142 = ISENABLER4 bit14 */
#define GIC_DISABLE 0x0C000190U
#define EINT_MASK 0x1000B080U
#define EINT_MASK_SET 0x1000B0C0U /* GPIO20 only */
#define FOREIGN_GATES ((1U << 12) | (1U << 13) | (1U << 14))

typedef struct {
  UINT32 Ccfg;
  UINT32 SourceHz;
  UINT32 Reason;
  UINT32 Gate;
  UINT32 Mux;
  UINT32 Mode175;
  UINT32 Mode176;
  UINT32 Start;
  UINT32 Control;
  UINT32 IntrMask;
  UINT32 McuIntr;
  UINT32 DmaEnable;
  UINT32 GicEnable;
  UINT32 EintMask;
  UINT32 IoPull;
  UINT32 IoRsel;
} CHARGER_NVS;

STATIC volatile CHARGER_NVS *mCharger = (VOID *)(UINTN)LANCELOT_CHARGER_NVS;
STATIC BOOLEAN mPrepared;
STATIC UINT32 mPreparedMux;

STATIC BOOLEAN Snapshot (VOID)
{
  mCharger->Gate = MmioRead32 (I2C_GATE);
  mCharger->Mux = MmioRead32 (I2C_MUX) & 0xFFU;
  mCharger->Mode175 = MmioRead32 (MODE175);
  mCharger->Mode176 = MmioRead32 (MODE176);
  mCharger->GicEnable = MmioRead32 (GIC_ENABLE);
  mCharger->EintMask = MmioRead32 (EINT_MASK);
  mCharger->IoPull = MmioRead32 (0x10002660U);
  mCharger->IoRsel = MmioRead32 (0x10002690U);
  if ((mCharger->Gate & (1U << 11)) != 0 ||
      (MmioRead32 (0x10001094U) & (1U << 18)) != 0) return FALSE;
  mCharger->Start = MmioRead16 (I2C7 + 0x24U);
  mCharger->Control = MmioRead16 (I2C7 + 0x10U);
  mCharger->IntrMask = MmioRead16 (I2C7 + 0x08U);
  mCharger->McuIntr = MmioRead16 (I2C7 + 0x40U);
  mCharger->DmaEnable = MmioRead32 (I2C7_DMA + 0x08U);
  return TRUE;
}

STATIC VOID LogState (CONST CHAR8 *Phase)
{
  BootLogPrint ("[WOA] CHARGER_HANDOFF phase=%a ccfg=0x%x cfq0_hz=%u reason=%u control_enabled=0 nvs=0x%x\n",
                Phase, mCharger->Ccfg, mCharger->SourceHz, mCharger->Reason, LANCELOT_CHARGER_NVS);
  BootLogPrint ("[WOA] I2C7_HANDOFF gate=0x%x mux=0x%x mode175=0x%x mode176=0x%x start=0x%x control=0x%x mask=0x%x mcu=0x%x dma=0x%x gic142=0x%x eint20=0x%x pull=0x%x rsel=0x%x\n",
                mCharger->Gate, mCharger->Mux, mCharger->Mode175, mCharger->Mode176,
                mCharger->Start, mCharger->Control, mCharger->IntrMask, mCharger->McuIntr,
                mCharger->DmaEnable, mCharger->GicEnable, mCharger->EintMask,
                mCharger->IoPull, mCharger->IoRsel);
}

VOID LancelotChargerInitialize (VOID)
{
  STATIC_ASSERT (sizeof (CHARGER_NVS) == 64, "charger NVS size");
  ZeroMem ((VOID *)mCharger, sizeof (*mCharger));
  mCharger->Ccfg = CHG_VERSION;
  mCharger->Reason = 1;
}

VOID LancelotChargerPrepare (VOID)
{
  MTK_CLOCK_PROTOCOL *Clock;
  UINT64 Frequency;
  UINT32 ClockId, DmaClockId;
  EFI_STATUS Status;
  mCharger->Reason = 2;
  Status = gBS->LocateProtocol (&gMediaTekClockProtocolGuid, NULL, (VOID **)&Clock);
  if (EFI_ERROR (Status)) goto Done;
  Status = Clock->GetId ("INFRA_I2C_AP", &ClockId);
  if (EFI_ERROR (Status)) goto Done;
  mCharger->Reason = 3;
  Status = Clock->SetEnable (ClockId, TRUE);
  if (EFI_ERROR (Status)) goto Done;
  mCharger->Reason = 4;
  Status = Clock->GetFrequency (ClockId, &Frequency);
  if (EFI_ERROR (Status) || Frequency < 5000000U || Frequency > 260000000U || Frequency % 5U != 0) goto Done;
  mCharger->SourceHz = (UINT32)Frequency;
  mCharger->Reason = 5;
  Status = Clock->GetId ("INFRA_AP_DMA", &DmaClockId);
  if (EFI_ERROR (Status)) goto Done;
  Status = Clock->SetEnable (DmaClockId, TRUE);
  if (EFI_ERROR (Status) || !Snapshot ()) goto Done;
  // This profile keeps LANCELOT_USB_HOST=0. Require non-AP clients clock-gated.
  // Never reset/drain a foreign or active transaction to fabricate ownership.
  mCharger->Reason = 6;
  if ((mCharger->Gate & FOREIGN_GATES) != FOREIGN_GATES) goto Done;
  mCharger->Reason = 7;
  if (mCharger->Start != 0 || (mCharger->Control & 4U) != 0 ||
      (mCharger->DmaEnable & 1U) != 0 || mCharger->McuIntr != 0) goto Done;
  MmioAndThenOr32 (MODE175, ~(UINT32)(15U << 28), 1U << 28);
  MmioAndThenOr32 (MODE176, ~(UINT32)15U, 1U);
  // Per-line mask only; preserve other EINT lines used by the board.
  MmioWrite32 (GIC_DISABLE, 1U << 14);
  MmioWrite32 (EINT_MASK_SET, 1U << 20);
  MmioWrite16 (I2C7 + 0x08U, 0);
  if (!Snapshot ()) goto Done;
  mCharger->Reason = 8;
  if ((mCharger->Mode175 >> 28) != 1 || (mCharger->Mode176 & 15U) != 1 ||
      (mCharger->GicEnable & (1U << 14)) != 0 || (mCharger->EintMask & (1U << 20)) == 0) goto Done;
  mPreparedMux = mCharger->Mux;
  mPrepared = TRUE;
  mCharger->Reason = 0;
  MemoryFence ();
  mCharger->Ccfg = CHG_VERSION | CHG_DIAGNOSTIC;
Done:
  MemoryFence ();
  LogState ("READY");
}

VOID LancelotChargerExit (VOID)
{
  // No protocol calls, allocation, timers or transactions at ExitBootServices.
  if (mPrepared && (!Snapshot () || mCharger->Mux != mPreparedMux ||
      (mCharger->Gate & FOREIGN_GATES) != FOREIGN_GATES || mCharger->Start != 0 ||
      (mCharger->Control & 4U) != 0 || (mCharger->DmaEnable & 1U) != 0 ||
      mCharger->McuIntr != 0 || mCharger->IntrMask != 0 ||
      (mCharger->Mode175 >> 28) != 1 || (mCharger->Mode176 & 15U) != 1 ||
      (mCharger->GicEnable & (1U << 14)) != 0 || (mCharger->EintMask & (1U << 20)) == 0)) {
    mCharger->Ccfg = CHG_VERSION;
    mCharger->Reason = 9;
  }
  MemoryFence ();
  LogState ("EBS");
}
