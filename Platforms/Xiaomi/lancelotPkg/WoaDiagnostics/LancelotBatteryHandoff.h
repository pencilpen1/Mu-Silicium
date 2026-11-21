/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef LANCELOT_BATTERY_HANDOFF_H
#define LANCELOT_BATTERY_HANDOFF_H

#define BATTERY_MAX_SAMPLES       100U
#define BATTERY_MAX_CLOCK_POLLS   20000U
#define BATTERY_COUNTER_HZ        13000000U
#define BATTERY_DEADLINE_TICKS    130000U
#define BATTERY_MIN_GAP_TICKS     1300U
#define BATTERY_MAX_GAP_TICKS     2600U
#define BATTERY_WACS_ENABLE       0x1000D090U
#define BATTERY_WACS_INIT_DONE    0x1000D094U
#define BATTERY_WACS_RDATA        0x1000DC24U
#define BATTERY_WACS_FSM_REQ      0x000F0000U
#define BATTERY_WACS_SYNC_IDLE    0x00100000U
#define BATTERY_WACS_INIT_BIT     0x00200000U

typedef enum {
  BatteryStable,
  BatteryOwnerNotClosed,
  BatteryBadClock,
  BatteryDeadline,
  BatteryInvalid,
  BatteryForeignPending,
  BatterySampleLimit
} BATTERY_TERMINAL;

typedef struct {
  UINT32 Enable, Init, State;
  UINT64 Completed;
} BATTERY_SAMPLE;

typedef struct {
  BATTERY_SAMPLE Samples[BATTERY_MAX_SAMPLES];
  UINT64 Start, Final;
  UINT32 Count, Streak;
} BATTERY_OBSERVATION;

typedef UINT64 (*BATTERY_COUNTER)(VOID);
typedef UINT32 (*BATTERY_REGISTER_READ)(UINTN Address);

STATIC inline VOID
BatteryReadSample (BATTERY_REGISTER_READ Read, BATTERY_COUNTER Counter, BATTERY_SAMPLE *Sample)
{
  Sample->Enable = Read (BATTERY_WACS_ENABLE);
  Sample->Init = Read (BATTERY_WACS_INIT_DONE);
  Sample->State = Read (BATTERY_WACS_RDATA);
  Sample->Completed = Counter ();
}

STATIC inline BATTERY_TERMINAL
BatteryObserve (BOOLEAN NormalApClosed, UINT64 Frequency, BATTERY_REGISTER_READ Read,
                BATTERY_COUNTER Counter, BATTERY_OBSERVATION *Observation)
{
  if (!NormalApClosed) return BatteryOwnerNotClosed;
  if (Frequency != BATTERY_COUNTER_HZ) return BatteryBadClock;
  Observation->Start = Observation->Final = Counter ();
  UINT64 LastCompletion = Observation->Start;
  for (UINT32 I = 0; I < BATTERY_MAX_SAMPLES; ++I) {
    BOOLEAN GapReached = I == 0;
    for (UINT32 Poll = 0; !GapReached && Poll < BATTERY_MAX_CLOCK_POLLS; ++Poll) {
      UINT64 Now = Counter ();
      if (Now < Observation->Final) return BatteryBadClock;
      Observation->Final = Now;
      if (Now - Observation->Start >= BATTERY_DEADLINE_TICKS) return BatteryDeadline;
      GapReached = Now - LastCompletion >= BATTERY_MIN_GAP_TICKS;
    }
    if (!GapReached) return BatteryBadClock;
    BATTERY_SAMPLE *Sample = &Observation->Samples[Observation->Count++];
    BatteryReadSample (Read, Counter, Sample);
    if (Sample->Completed < Observation->Final) return BatteryBadClock;
    Observation->Final = Sample->Completed;
    if (Sample->Completed - Observation->Start >= BATTERY_DEADLINE_TICKS) return BatteryDeadline;
    if (Sample->Enable == 0xFFFFFFFFU || !(Sample->Enable & 1U) || Sample->Init != 1U ||
        Sample->State == 0xFFFFFFFFU || !(Sample->State & BATTERY_WACS_INIT_BIT)) return BatteryInvalid;
    // A command appearing after the known producer closed is not ours to drain.
    if (Sample->State & BATTERY_WACS_FSM_REQ) return BatteryForeignPending;
    if (!(Sample->State & BATTERY_WACS_SYNC_IDLE)) Observation->Streak = 0;
    else if (I == 0 || Sample->Completed - LastCompletion > BATTERY_MAX_GAP_TICKS) Observation->Streak = 1;
    else ++Observation->Streak;
    LastCompletion = Sample->Completed;
    if (Observation->Streak == 3) {
      UINT64 Now = Counter ();
      if (Now < Observation->Final) return BatteryBadClock;
      Observation->Final = Now;
      return Now - Observation->Start < BATTERY_DEADLINE_TICKS ? BatteryStable : BatteryDeadline;
    }
  }
  return BatterySampleLimit;
}

#endif
