/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Additive I2C7 polling handoff. No SMB1351 transactions or charge writes.
 * Evidence gate preserves diagnostic-only behavior with missing board proof.
 */
#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BootLogLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/MtkClock.h>
#include "lancelot_charger_handoff.h"
#include "ChargerEvidenceGate.h"

#define CHG_VERSION 0x00010000U
#define CHG_DIAGNOSTIC 0x31U
#define I2C7 0x11004000U
#define I2C7_DMA 0x11000680U
#define I2C_GATE 0x10001090U
#define I2C_GATE_SET 0x10001080U
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
STATIC BOOLEAN mSnapshotValid;
STATIC UINT32 mDmaGate;
STATIC UINT16 mIntrStatus;
STATIC UINT16 mFifoStatus;

STATIC BOOLEAN Snapshot (VOID)
{
  // Always take a fresh snapshot, including when preparation was rejected.
  // The validity flag distinguishes unclocked registers from measured zeroes.
  mSnapshotValid = FALSE;
  mCharger->Gate = MmioRead32 (I2C_GATE);
  mCharger->Mux = MmioRead32 (I2C_MUX) & 0xFFU;
  mCharger->Mode175 = MmioRead32 (MODE175);
  mCharger->Mode176 = MmioRead32 (MODE176);
  mCharger->GicEnable = MmioRead32 (GIC_ENABLE);
  mCharger->EintMask = MmioRead32 (EINT_MASK);
  mCharger->IoPull = MmioRead32 (0x10002660U);
  mCharger->IoRsel = MmioRead32 (0x10002690U);
  mDmaGate = MmioRead32 (0x10001094U);
  if ((mCharger->Gate & (1U << 11)) != 0 ||
      (mDmaGate & (1U << 18)) != 0) return FALSE;
  mCharger->Start = MmioRead16 (I2C7 + 0x24U);
  mCharger->Control = MmioRead16 (I2C7 + 0x10U);
  mCharger->IntrMask = MmioRead16 (I2C7 + 0x08U);
  mCharger->McuIntr = MmioRead16 (I2C7 + 0x40U);
  mCharger->DmaEnable = MmioRead32 (I2C7_DMA + 0x08U);
  mIntrStatus = MmioRead16 (I2C7 + 0x0CU);
  mFifoStatus = MmioRead16 (I2C7 + 0xF4U);
  mSnapshotValid = TRUE;
  return TRUE;
}

STATIC BOOLEAN Idle (VOID)
{
  return mSnapshotValid && mCharger->Start == 0 &&
         (mCharger->Control & 4U) == 0 && (mCharger->DmaEnable & 1U) == 0;
}

STATIC BOOLEAN PollingReady (VOID)
{
  return Idle () && (mCharger->Gate & FOREIGN_GATES) == FOREIGN_GATES &&
         mCharger->McuIntr == 0 && mCharger->IntrMask == 0 &&
         (mCharger->Mode175 >> 28) == 1 && (mCharger->Mode176 & 15U) == 1 &&
         (mCharger->GicEnable & (1U << 14)) == 0 &&
         (mCharger->EintMask & (1U << 20)) != 0;
}

STATIC BOOLEAN ObserveIdle (VOID)
{
  // I2C7 has no CCU channel offset in the lancelot DT. Do not reset, drain,
  // clear START, or terminate an inherited request. Require a stable idle
  // window before isolating its non-AP clocks, then verify it again afterwards.
  UINTN Count;
  for (Count = 0; Count < 10; Count++) {
    if (!Snapshot () || !Idle ()) return FALSE;
    MicroSecondDelay (1000);
  }
  return Snapshot () && Idle ();
}

// There is deliberately no approved board profile or same-boot SMB1351
// observation provider here. Neither a historical PE payload nor a DT timer
// value can be promoted to one. Missing evidence is a logged failure, not a
// request to run charger transactions at EBS or alter the established clocks.
STATIC VOID PublishEvidence (CONST CHAR8 *Phase)
{
  CHARGER_GATE_RESULT Gate = ChargerEvaluateEvidence (
    mPrepared && mCharger->Reason == 0 && PollingReady (), NULL, NULL);
  mCharger->Ccfg = CHG_VERSION | Gate.Permit;
  MemoryFence ();
  BootLogPrint ("[WOA] CHARGER_EVIDENCE phase=%a version=1 profile=0 current_smb_valid=0 permit=0x%x\n",
                Phase, Gate.Permit);
  BootLogPrint ("[WOA] CHARGER_BLOCK bit=0x2 missing=0x%x requirement=writers_quiesced_stop_authority_action_current_identity\n",
                Gate.ControlMissing);
  BootLogPrint ("[WOA] CHARGER_BLOCK bit=0x4 missing=0x%x requirement=ntc_route_thresholds_action_recovery_independent_stop\n",
                Gate.ThermalMissing);
  BootLogPrint ("[WOA] CHARGER_BLOCK bit=0x8 missing=0x%x requirement=timer_enable_duration_pause_reset_independent_stop\n",
                Gate.TimerMissing);
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
  BootLogPrint ("[WOA] I2C7_SNAPSHOT phase=%a controller_valid=%u dma_gate=0x%x intr=0x%x fifo=0x%x polling_ready=%u\n",
                Phase, mSnapshotValid, mDmaGate, mIntrStatus, mFifoStatus, PollingReady ());
}

VOID LancelotChargerInitialize (VOID)
{
  STATIC_ASSERT (sizeof (CHARGER_NVS) == 64, "charger NVS size");
  ZeroMem ((VOID *)mCharger, sizeof (*mCharger));
  mCharger->Ccfg = CHG_VERSION;
  mCharger->Reason = 1;
  mPrepared = FALSE;
  mSnapshotValid = FALSE;
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
  LogState ("BEFORE_ISOLATION");
  mCharger->Reason = 7;
  if (!ObserveIdle ()) goto Done;
  // This WOA profile omits MusbHostDxe and has no subsequent I2C7 client.
  // IFR2 bit12/13/14 isolate CCU/SSPM/reserved I2C clock paths. Do not call
  // Clock->SetEnable(FALSE): ClockDxe recursively disables the common I2C
  // mux/PLL parent, including AP and other peripherals. The W1S alias changes
  // exactly these gates and leaves AP, APDMA, and the common parent running.
  mCharger->Reason = 6;
  MmioWrite32 (I2C_GATE_SET, FOREIGN_GATES);
  MemoryFence ();
  if (!Snapshot () || (mCharger->Gate & FOREIGN_GATES) != FOREIGN_GATES ||
      !ObserveIdle ()) goto Done;
  LogState ("NON_AP_ISOLATED");
  MmioAndThenOr32 (MODE175, ~(UINT32)(15U << 28), 1U << 28);
  MmioAndThenOr32 (MODE176, ~(UINT32)15U, 1U);
  // Per-line mask only; preserve other EINT lines used by the board.
  MmioWrite32 (GIC_DISABLE, 1U << 14);
  MmioWrite32 (EINT_MASK_SET, 1U << 20);
  // MCU_INTR is an interrupt enable/routing register, not an ownership token
  // (the Android v2 transfer path programs it before START). With the engine
  // idle and non-AP clocks isolated, disable its inherited interrupt routes
  // together with INTR_MASK for the Windows polling contract. No FIFO/status
  // clearing, controller reset, DMA reset, or charger transaction is needed.
  MmioWrite16 (I2C7 + 0x08U, 0);
  MmioWrite16 (I2C7 + 0x40U, 0);
  if (!Snapshot ()) goto Done;
  mCharger->Reason = 8;
  if (!PollingReady ()) goto Done;
  mPreparedMux = mCharger->Mux;
  mPrepared = TRUE;
  mCharger->Reason = 0;
  MemoryFence ();
  mCharger->Ccfg = CHG_VERSION | CHG_DIAGNOSTIC;
Done:
  MemoryFence ();
  PublishEvidence ("READY");
  LogState ("READY");
}

VOID LancelotChargerExit (VOID)
{
  // No protocol calls, allocation, timers or transactions at ExitBootServices.
  BOOLEAN Valid = Snapshot ();
  if (mPrepared && (!Valid || mCharger->Mux != mPreparedMux || !PollingReady ())) {
    mCharger->Ccfg = CHG_VERSION;
    mCharger->Reason = 9;
  }
  MemoryFence ();
  PublishEvidence ("EBS");
  LogState ("EBS");
}
