/* SPDX-License-Identifier: MIT
 * Bounded private Mali-G52 WRITE_VALUE transaction, independent of graphics
 * APIs. Register/descriptor facts are referenced in BOOT54-NATIVE.zh-CN.md.
 * No resource grant, DMA allocation or fabricated completion exists here.
 */
#ifndef LGPU_NATIVE_JOB_H
#define LGPU_NATIVE_JOB_H
#include "lgpu_mmu.h"
#define LGJ_VERSION 1u
#define LGJ_MAGIC UINT32_C(0x3152474a) /* JGR1, not a firmware grant */
#define LGJ_ACPI_SIGNATURE UINT32_C(0x4a42474c) /* LGBJ */
#define LGJ_ROUTE_WINDOWS 1u
#define LGJ_ROUTE_PREBOOT 2u
#define LGJ_PAGES 7u /* boundary guards + 3 tables + job + output */
#define LGJ_BYTES (LGJ_PAGES*LGPU_PAGE_SIZE)
#define LGJ_TABLE_PAGES 3u
#define LGJ_JOB_PAGE 4u
#define LGJ_OUTPUT_PAGE 5u
#define LGJ_JOB_VA UINT64_C(0x02000000)
#define LGJ_OUTPUT_VA (LGJ_JOB_VA+LGPU_PAGE_SIZE)
#define LGJ_VALUE UINT32_C(0x4c474a32)
#define LGJ_TARGET_WORD 32u
#define LGJ_TOTAL_US UINT64_C(50000)
#define LGJ_CLEANUP_RESERVE_US UINT64_C(10000)
#define LGJ_POLL_US UINT64_C(10000)
#define LGJ_POLL_LIMIT 10001u
#define LGJ_TRACE_MAX 768u
#define LGJ_NOT_RUN (-4096)
#define LGJ_REQUESTED 1u
#define LGJ_AUTHORITY_VALID 2u
#define LGJ_MEMORY_VALID 4u
#define LGJ_ID_VALID 8u
#define LGJ_GPU_MUTATED 16u
#define LGJ_AS_LIVE 32u
#define LGJ_SUBMITTED 64u
#define LGJ_FENCE_COMPLETE 128u
#define LGJ_READBACK_VALID 256u
#define LGJ_SAFE_TO_FREE 512u
#define LGJ_READY_TO_RETURN 1024u
#define LGJ_UNKNOWN 2048u
#define LGJ_MMIO_LOST 4096u
#define LGJ_RESOURCES_RETURNED 8192u
#define LGJ_FLAGS 16383u
#define IOCTL_LGPU_NATIVE_JOB UINT32_C(0x0022e05c) /* read/write, command-only */
enum lgj_command { LGJ_STATUS, LGJ_ONCE };
enum lgj_stage {
    LGJ_OWNER, LGJ_MEMORY, LGJ_FEATURES, LGJ_IDLE, LGJ_RESET, LGJ_CONFIG,
    LGJ_CORE_ON, LGJ_TABLES, LGJ_MMU, LGJ_CACHE_BEFORE, LGJ_SUBMIT,
    LGJ_FENCE, LGJ_CACHE_AFTER, LGJ_DETACH, LGJ_CPU_READBACK,
    LGJ_CORE_OFF, LGJ_RESTORE, LGJ_MEMORY_RETURN, LGJ_POWER_RETURN,
    LGJ_STAGES
};
enum lgj_trace_kind { LGJ_REG_READ,LGJ_REG_WRITE,LGJ_CACHE_DEVICE,LGJ_CACHE_CPU,LGJ_GUARD };
struct lgj_request { uint32_t size,version,command,reserved; };
struct lgj_step {
    uint32_t executed,completed,reads,writes;
    int32_t error;uint32_t reserved;
    uint64_t adapter_status,start_us,end_us;
};
struct lgj_trace {
    uint32_t kind,offset,value,stage;
    int32_t error;uint32_t reserved;
    uint64_t adapter_status,at_us;
};
struct lgj_report {
    uint32_t magic,size,version,route,command,request_complete,stop_stage,flags;
    int32_t error;uint32_t used,gpu_reads,gpu_writes,cache_device,cache_cpu,
        guard_checks,trace_count,trace_dropped,first_failed_offset;
    uint32_t gpu_id,mmu_features,as_present,js_present,slot_features,
        va_bits,pa_bits,identity_reads_in_core,table_pages,mapped_pages;
    uint64_t shader_present,tiler_present,l2_present;
    uint64_t start_us,work_start_us,end_us,deadline_us,work_deadline_us;
    uint64_t generation,root_dma,job_va,output_va,fence,completion_generation;
    uint64_t max_poll_gap_us,last_busy_us,done_observed_us;
    uint64_t adapter_status;
    uint32_t job_irq,mmu_irq,gpu_irq,js_status,js_next_command,js_state,
        fence_job_irq,fence_js_status;
    uint32_t gpu_fault_status,as_fault_status;
    uint64_t gpu_fault_address,as_fault_address,job_head,job_tail;
    uint32_t expected_value,observed_value,descriptor_exception,bad_word,bad_page,
        guards_valid,table_guards_valid,backing_retained,as_detached,core_off,
        config_restored,memory_returned,power_returned;
    uint32_t initial_config[5],final_config[5],initial_irq_mask[3],final_irq_mask[3];
    uint32_t descriptor_before[16],descriptor_after[16];
    uint8_t table_sha256[3][32],job_before_sha256[32],job_after_sha256[32],
        output_before_sha256[32],output_after_sha256[32];
    /* Intake credits and exact raw NTSTATUS (Windows only). Opaque power
     * tokens stay in the kernel adapter, never serialized to user mode. */
    uint32_t discovery_executed,interface_executed,acquire_executed,d0_pinned,
        provider_caps,header_held,pending_queries,resources_retained,
        current_command,request_admitted,status_snapshot,repeated_request,
        unresolved_queries,power_lease_held;
    int32_t discovery_status,interface_status,acquire_status,release_status;
    uint64_t consumer_lease_generation;
    uint64_t actual_owner_generation,actual_profile_generation,actual_owner_fixed_deadline_us;
    uint64_t actual_dma_base;
    uint32_t cpu_current_el,cpu_cache_line,cpu_page_attributes[LGJ_PAGES],
        source_scope_checks,source_aon_reads,source_pmic_reads,source_mfg_reads;
    struct lgj_step steps[LGJ_STAGES];
    struct lgj_trace traces[LGJ_TRACE_MAX];
};
#pragma pack(push,1)
struct lgj_preboot_record {
    uint32_t signature,length;
    uint8_t acpi_revision,checksum,oem_id[6],oem_table_id[8];
    uint32_t oem_revision,creator_id,creator_revision;
    uint32_t version,board_id,route,reserved,padding;
    uint64_t boot_id,source_generation;
    uint8_t firmware_source_sha256[32],firmware_text_sha256[32];
    struct lgj_report report;
};
#pragma pack(pop)
_Static_assert(sizeof(struct lgj_request)==16,"native command ABI1");
_Static_assert(sizeof(struct lgj_step)==48,"native step ABI1");
_Static_assert(sizeof(struct lgj_trace)==40,"native trace ABI1");
_Static_assert(offsetof(struct lgj_preboot_record,report)==136,"LGBJ payload offset");
struct lgj_memory { void *cpu;uint64_t dma_base;uint32_t pages,reserved; };
struct lgj_ops {
    void *context;
    uint64_t (*now_us)(void *);
    void (*delay_us)(void *,uint32_t);
    /* Real original-owner hold/scope/lifecycle, with its fixed deadline.
     * Not a flag, a receipt copy, a new writer lock or a constant success. */
    int (*validate)(void *,unsigned stage,uint64_t deadline,uint64_t *raw_status);
    int (*check_memory)(void *,const struct lgj_memory *,uint64_t *raw_status);
    int (*read32)(void *,uint32_t,uint32_t *,uint64_t *raw_status);
    int (*write32)(void *,uint32_t,uint32_t,uint64_t *raw_status);
    /* Whole owned 4K pages, explicit completed CPU/device ownership change.
     * Callers implement real PoC maintenance / HAL common-buffer semantics. */
    int (*cache)(void *,unsigned page,unsigned for_cpu,uint64_t *raw_status);
    void (*barrier)(void *);
    void (*event)(void *,unsigned stage,unsigned completed,int error,uint64_t raw_status);
};
struct lgj_context {
    struct lgj_ops io;struct lgj_memory memory;struct lgj_report report;
    struct lgpu_vm vm;struct lgpu_table tables[LGJ_TABLE_PAGES];
    uint64_t last_us,stage_deadline;
    unsigned initialized,used,current_stage,cleanup,io_failed;
};
void lgj_initialize(struct lgj_context *,unsigned route);
void lgj_begin_step(struct lgj_context *,unsigned stage);
int lgj_end_step(struct lgj_context *,int error,uint64_t raw_status);
/* identity_already_read is internal-only: preboot passes its ONE actual ID
 * read immediately before this callback. Windows passes zero and reads once.
 * Caller preallocates context/memory; no allocations in this core.
 * Once GPU mutation begins, any unknown/late/transport failure retains ALL
 * hardware backing. No automatic reset, retry, force-drain or fresh budget.
 */
int lgj_run(struct lgj_context *,const struct lgj_ops *,const struct lgj_memory *,
            uint64_t generation,uint32_t actual_identity,unsigned identity_already_read);
unsigned lgj_read_allowed(uint32_t);
unsigned lgj_write_allowed(const struct lgj_context *,uint32_t,uint32_t);
unsigned lgj_report_valid(const struct lgj_report *,uint32_t bytes);
void lgj_seal_preboot(struct lgj_preboot_record *);
unsigned lgj_preboot_valid(const struct lgj_preboot_record *,uint32_t bytes,uint64_t boot);
const char *lgj_stage_name(unsigned);
#endif
