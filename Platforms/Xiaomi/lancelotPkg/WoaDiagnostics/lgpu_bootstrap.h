/* SPDX-License-Identifier: MIT
 * A separate, experimental pre-EBS AP/TPL diagnostic. This receipt never
 * implements or qualifies LGPU_POWER_INTERFACE or a Windows runtime source.
 */
#ifndef LGPU_BOOTSTRAP_H
#define LGPU_BOOTSTRAP_H
#include <stdint.h>
#include <stddef.h>
#define LGBS_VERSION 1u
#define LGBS_BOARD 0x6768u
#define LGBS_SIGNATURE 0x5342474cu /* ACPI bytes LGBS; no namespace device. */
#define LGBS_ERROR_BIT (UINT64_C(1)<<63)
#define LGBS_SUCCESS UINT64_C(0)
#define LGBS_INVALID (LGBS_ERROR_BIT|2u)
#define LGBS_UNSUPPORTED (LGBS_ERROR_BIT|3u)
#define LGBS_NOT_READY (LGBS_ERROR_BIT|6u)
#define LGBS_IO (LGBS_ERROR_BIT|7u)
#define LGBS_TIMEOUT (LGBS_ERROR_BIT|18u)
#define LGBS_ABORTED (LGBS_ERROR_BIT|21u)
#define LGBS_NOT_RUN UINT64_MAX
#define LGBS_TOTAL_US UINT64_C(2000000)
#define LGBS_RESTORE_RESERVE_US UINT64_C(500000)
#define LGBS_STEP_US UINT64_C(200000)
#define LGBS_ACK_US UINT64_C(10000)
#define LGBS_MAX_POLLS 1001u
#define LGBS_TRACES 512u
#define LGBS_PMIC_FIELDS 12u
#define LGBS_CLOCK_FIELDS 11u
#define LGBS_AON_FIELDS 8u
#define LGBS_DISABLED 0u
#define LGBS_IDENTITY_ONCE 1u       /* only a fully accessible inherited island */
#define LGBS_COLD_IDENTITY_ONCE 2u  /* only a fully normalized cold/off island */
#define LGBS_PHASE_RUNNING 1u
#define LGBS_PHASE_RETURNED 2u     /* receipt of an earlier pre-EBS operation */
#define LGBS_PHASE_TERMINAL 3u
#define LGBS_REQUESTED 1u
#define LGBS_AP_OFF_VERIFIED 2u
#define LGBS_TPL_HELD 4u
#define LGBS_RAILS_VALID 8u
#define LGBS_CLOCK_VALID 16u
#define LGBS_ISLAND_LIVE 32u
#define LGBS_ID_READ 64u
#define LGBS_ID_VALID 128u
#define LGBS_MUTATION_ISSUED 256u
#define LGBS_RETURN_VERIFIED 512u
#define LGBS_SCOPE_RETURNED 1024u
#define LGBS_BOOT_MAY_CONTINUE 2048u
#define LGBS_TERMINAL_UNKNOWN 4096u
#define LGBS_FLAGS 8191u
enum lgbs_stage {
    LGBS_PROTOCOL, LGBS_SCOPE, LGBS_PMIC, LGBS_CLOCK, LGBS_PREFLIGHT,
    LGBS_POWER_ON, LGBS_IDENTITY, LGBS_POWER_OFF, LGBS_READBACK,
    LGBS_SCOPE_RETURN, LGBS_STAGES
};
enum lgbs_bank { LGBS_SPM, LGBS_INFRA, LGBS_MFG, LGBS_GPU, LGBS_TOP, LGBS_APMIXED, LGBS_BANKS };
enum lgbs_trace_kind { LGBS_MMIO_READ, LGBS_MMIO_WRITE, LGBS_PMIC_READ, LGBS_AFFINITY };
#pragma pack(push,1)
struct lgbs_step {
    uint32_t executed, completed, reads, writes;
    uint64_t status, start_us, end_us;
};
struct lgbs_trace {
    uint32_t kind, address, value, valid;
    uint64_t status, at_us;
};
struct lgbs_record {
    uint32_t signature, length;
    uint8_t acpi_revision, checksum, oem_id[6], oem_table_id[8];
    uint32_t oem_revision, creator_id, creator_revision;
    uint32_t version, board_id, mode, phase, flags, stage_count, trace_count, trace_dropped;
    uint32_t read_attempts, write_attempts, pmic_read_attempts, scope_checks;
    uint32_t ap_off_before, ap_off_after, ap_on_mask, ap_unknown_mask;
    uint32_t mpidr_before, mpidr_after, configured_hz, vgpu_uv, vsram_uv;
    uint32_t gpu_id, gpu_id_valid, failed_stage, failed_address;
    uint64_t boot_id, source_generation, counter_frequency, start_us, end_us, deadline_us;
    uint64_t status, restore_status, release_status;
    uint16_t pmic_before[LGBS_PMIC_FIELDS], pmic_after[LGBS_PMIC_FIELDS];
    uint32_t clock_before[LGBS_CLOCK_FIELDS], clock_after[LGBS_CLOCK_FIELDS];
    uint32_t aon_before[LGBS_AON_FIELDS], aon_after[LGBS_AON_FIELDS];
    uint32_t mfg_gate_before, mfg_gate_before_valid;
    uint8_t firmware_source_sha256[32], firmware_text_sha256[32];
    struct lgbs_step steps[LGBS_STAGES];
    struct lgbs_trace traces[LGBS_TRACES];
};
#pragma pack(pop)
_Static_assert(sizeof(struct lgbs_step)==40,"bootstrap step ABI1");
_Static_assert(sizeof(struct lgbs_trace)==32,"bootstrap trace ABI1");
_Static_assert(sizeof(struct lgbs_record)==17264,"bootstrap ACPI ABI1");

struct lgbs_scope_observation {
    uint32_t tpl_held, mpidr, ap_off_mask, ap_on_mask, ap_unknown_mask;
    int64_t affinity[8]; /* original PSCI AFFINITY_INFO results, including errors */
};
struct lgbs_ops {
    void *context;
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *,uint32_t);
    /* Actual UEFI TPL16 window + BSP + read-only PSCI. Never CPU_OFF/ON. */
    uint64_t (*scope)(void *,struct lgbs_scope_observation *);
    uint64_t (*scope_return)(void *); /* invokes the original RestoreTPL */
    /* Original checked PWRAP protocol, including its original lock/poison. */
    uint64_t (*pmic_read16)(void *,uint16_t,uint16_t *);
    /* Original firmware device mappings only. No PWRAP mapping or writes. */
    uint64_t (*read32)(void *,enum lgbs_bank,uint32_t,uint32_t *);
    uint64_t (*write32)(void *,enum lgbs_bank,uint32_t,uint32_t);
    /* Optional bounded RAM-only stage logger, never a resource callback. */
    void (*stage_event)(void *,uint32_t stage,uint32_t completed,uint64_t status);
};
struct lgbs_context {
    struct lgbs_ops io;
    struct lgbs_record record;
    uint64_t last_us, step_started_us;
    uint32_t current_stage, used, restoring, io_failed, scope_lost;
};
/* Caller supplies the actual same-boot LGSR provenance and preallocated state.
 * No allocation, protocol discovery, console/USB I/O, or EFI table update in
 * the timed core. The original RAM-only logger may record phase boundaries.
 * A used context cannot be retried or reset after uncertainty.
 * The AP/TPL scope is deliberately NOT all-peer ownership. Do not invoke
 * this diagnostic from Windows, EBS, S0 resume, jobs, DMA or a runtime lease.
 */
uint64_t lgbs_run(struct lgbs_context *,const struct lgbs_ops *,uint32_t mode,
                 uint64_t boot,uint64_t source_generation,uint64_t frequency);
void lgbs_seal(struct lgbs_record *);
unsigned lgbs_record_valid(const struct lgbs_record *,uint32_t bytes,uint64_t boot);
const char *lgbs_stage_name(unsigned);
uint16_t lgbs_pmic_address(unsigned);
uint32_t lgbs_clock_offset(unsigned);
uint32_t lgbs_aon_offset(unsigned);
#endif
