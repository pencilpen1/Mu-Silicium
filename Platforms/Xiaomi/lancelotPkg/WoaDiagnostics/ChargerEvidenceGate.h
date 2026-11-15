/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Pure evidence gate, with no MMIO, allocation or protocol calls. An evidence
 * bit means a reviewed board-specific result, not a request to enable a test.
 * boot19 has no approved proof or same-boot SMB1351 snapshot in firmware.
 */
#ifndef LANCELOT_CHARGER_EVIDENCE_GATE_H
#define LANCELOT_CHARGER_EVIDENCE_GATE_H
#include <Uefi.h>

#define CHG_PROOF_CONTROL 0x003U /* writers quiesced; stop authority/action */
#define CHG_PROOF_THERMAL 0x03CU /* NTC route; thresholds; action/recovery; independent stop */
#define CHG_PROOF_TIMERS  0x3C0U /* enable; duration; pause/reset; independent stop */
#define CHG_PROOF_ALL     0x3FFU
#define CHG_MISSING_PROFILE  0x80000000U
#define CHG_MISSING_CURRENT  0x40000000U
#define CHG_MISSING_MATCH    0x20000000U
#define CHG_MISSING_POLLING  0x10000000U

typedef struct {
  UINT32 ProfileId;
  UINT32 Verified;
  UINT8 Expected[72];
  UINT8 Mask[72];
} CHARGER_BOARD_PROOF;
typedef struct {
  BOOLEAN SameBoot;
  BOOLEAN Fresh;
  UINT8 Raw[72];
  UINT8 Valid[72];
} CHARGER_CURRENT_EVIDENCE;
typedef struct {
  UINT32 Permit;
  UINT32 ControlMissing;
  UINT32 ThermalMissing;
  UINT32 TimerMissing;
} CHARGER_GATE_RESULT;

STATIC CHARGER_GATE_RESULT ChargerEvaluateEvidence (
  BOOLEAN PollingValid,
  CONST CHARGER_BOARD_PROOF *Proof,
  CONST CHARGER_CURRENT_EVIDENCE *Current)
{
  STATIC CONST UINT8 Regs[] = {2,5,6,7,8,0x0B,0x0E,0x11,0x2E,0x32,0x3F};
  STATIC CONST UINT8 Masks[] = {0x80,0x0F,0x70,0x1F,0x7F,0xFF,0x3C,0xC0,0xFF,0x10,0xFF};
  CHARGER_GATE_RESULT Out = {0,CHG_PROOF_CONTROL,CHG_PROOF_THERMAL,CHG_PROOF_TIMERS};
  UINT32 Common = 0;
  UINTN Index;
  if (PollingValid) Out.Permit = 0x31U;
  else Common |= CHG_MISSING_POLLING;
  if (Proof == NULL || Proof->ProfileId == 0 || (Proof->Verified & ~CHG_PROOF_ALL) != 0) {
    Common |= CHG_MISSING_PROFILE;
  } else {
    Out.ControlMissing &= ~Proof->Verified;
    Out.ThermalMissing &= ~Proof->Verified;
    Out.TimerMissing &= ~Proof->Verified;
  }
  if (Current == NULL || !Current->SameBoot || !Current->Fresh) {
    Common |= CHG_MISSING_CURRENT;
  } else if (Proof != NULL && Proof->ProfileId != 0) {
    if (!Current->Valid[0x2E] || !Current->Valid[0x3F] ||
        Current->Raw[0x2E] != 0x30 || Current->Raw[0x3F] != 0x13) Common |= CHG_MISSING_MATCH;
    for (Index = 0; Index < sizeof (Regs); Index++) {
      UINT8 R = Regs[Index];
      // Test control and protection groups separately: a lost temperature
      // or timer assertion must not remove an independently justified STOP.
      UINT32 Group = R == 2 || R == 6 || R == 0x2E || R == 0x3F ? 1U :
                     R == 5 || R == 8 || R == 0x11 ? 3U : 2U;
      if (!Current->Valid[R] || (Proof->Mask[R] & Masks[Index]) != Masks[Index] ||
          ((Current->Raw[R] ^ Proof->Expected[R]) & Proof->Mask[R]) != 0) {
        if (Group == 1) Out.ControlMissing |= CHG_MISSING_MATCH;
        else if (Group == 2) Out.ThermalMissing |= CHG_MISSING_MATCH;
        else Out.TimerMissing |= CHG_MISSING_MATCH;
      }
    }
    if ((Current->Raw[6] & 0x70U) != 0 || (Current->Raw[2] & 0x80U) == 0)
      Out.ControlMissing |= CHG_MISSING_MATCH;
    if ((Current->Raw[7] & 0x10U) != 0 || (Current->Raw[0x32] & 0x10U) != 0)
      Out.ThermalMissing |= CHG_MISSING_MATCH;
    if ((Current->Raw[0x11] & 0xC0U) != 0 || (Current->Raw[8] & 1U) != 0)
      Out.TimerMissing |= CHG_MISSING_MATCH;
  }
  Out.ControlMissing |= Common;
  Out.ThermalMissing |= Common;
  Out.TimerMissing |= Common;
  if (Out.ControlMissing == 0) {
    Out.Permit |= 2U;
    if (Out.ThermalMissing == 0) Out.Permit |= 4U;
    if (Out.TimerMissing == 0) Out.Permit |= 8U;
  }
  return Out;
}
#endif
