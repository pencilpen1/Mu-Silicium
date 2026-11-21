/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "host.h"
#include "LancelotBatteryHandoff.h"

static BATTERY_OBSERVATION observation;
static UINT64 clock_ticks;
static UINT32 registers[8], sample_index, register_reads;
static UINT32 init, enable;
static BOOLEAN frozen, jump_on_final_read;

static UINT64 Counter(void) {
    if (!frozen) clock_ticks += 13;
    return clock_ticks;
}

static UINT32 Read(UINTN address) {
    ++register_reads;
    if (address == BATTERY_WACS_ENABLE) return enable;
    if (address == BATTERY_WACS_INIT_DONE) return init;
    assert(address == BATTERY_WACS_RDATA);
    if (jump_on_final_read && sample_index == 2) clock_ticks = BATTERY_DEADLINE_TICKS + 14;
    return registers[(sample_index++) % ARRAY_SIZE(registers)];
}

static void reset(UINT32 value) {
    memset(&observation, 0, sizeof(observation));
    for (unsigned i = 0; i < ARRAY_SIZE(registers); ++i) registers[i] = value;
    sample_index = register_reads = 0;
    clock_ticks = 0;
    frozen = jump_on_final_read = false;
    init = enable = 1;
}

static BATTERY_TERMINAL observe(BOOLEAN closed) {
    return BatteryObserve(closed, BATTERY_COUNTER_HZ, Read, Counter, &observation);
}

int main(void) {
    const UINT32 good = 0x80f0000e, syncing = 0x8060000e;
    reset(good); assert(observe(false) == BatteryOwnerNotClosed && register_reads == 0);
    reset(good); assert(BatteryObserve(true, 0, Read, Counter, &observation) == BatteryBadClock && !register_reads);
    reset(good); assert(observe(true) == BatteryStable && observation.Count == 3 && register_reads == 9);
    reset(good); registers[0] = syncing; registers[2] = syncing;
    assert(observe(true) == BatteryStable && observation.Count == 6 && observation.Streak == 3);
    reset(good); registers[1] = good | (6U << 16);
    assert(observe(true) == BatteryForeignPending && observation.Count == 2);
    reset(syncing); assert(observe(true) == BatteryDeadline && observation.Count <= BATTERY_MAX_SAMPLES);
    reset(good); init = 0; assert(observe(true) == BatteryInvalid && observation.Count == 1);
    reset(good); registers[0] = UINT32_MAX; assert(observe(true) == BatteryInvalid);
    reset(good); frozen = true; assert(observe(true) == BatteryBadClock && observation.Count == 1);
    reset(good); jump_on_final_read = true;
    assert(observe(true) == BatteryDeadline && observation.Count == 3);
    puts("WACS2 EBS close requirement, sync recovery, pending, invalid, stuck clock and late sample PASS");
    return 0;
}
