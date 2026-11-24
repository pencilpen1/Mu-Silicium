/* SPDX-License-Identifier: MIT */
#ifndef LGPU_HW_H
#define LGPU_HW_H

#include <stddef.h>
#include <stdint.h>

/* Register facts and board provenance: docs/HARDWARE.zh-CN.md. */
#define LGPU_GPU_BASE UINT64_C(0x13040000)
#define LGPU_GPU_SIZE 0x4000u
#define LGPU_MFG_BASE UINT64_C(0x13000000)
#define LGPU_MFG_SIZE 0x1000u
/* Historical name retained for ABI/source compatibility. BOOT54 physically
 * read this value once before EBS; Windows runtime has not read it. MMU feature
 * 0x2823 below still needs a live read before native jobs may program tables. */
#define LGPU_OBSERVED_GPU_ID 0x74021000u
#define LGPU_OBSERVED_MMU_FEATURES 0x2823u
#define LGPU_IRQ_COUNT 5u
#define LGPU_GSI_FIRST 306u /* DT SPI 274 + the 32 private GIC interrupt IDs. */
#define LGPU_MAX_RESOURCES 7u

enum lgpu_status {
    LGPU_OK = 0,
    LGPU_E_ARGUMENT = -1,
    LGPU_E_RESOURCE = -2,
    LGPU_E_UNSUPPORTED = -3,
    LGPU_E_IO = -4,
    LGPU_E_POWER = -5,
    LGPU_E_CLOCK = -6,
    LGPU_E_BUS = -7,
    LGPU_E_NO_POWER_PROVIDER = -8,
    LGPU_E_NOMEM = -9,
    LGPU_E_CONFLICT = -10,
    LGPU_E_NOT_MAPPED = -11,
    LGPU_E_CORRUPT = -12,
    LGPU_E_TIMEOUT = -13,
    LGPU_E_BUSY = -14,
    LGPU_E_QUARANTINED = -15,
    LGPU_E_PROVIDER_ABI = -16,
    LGPU_E_RELEASE = -17,
    LGPU_E_NOT_PROBED = -18,
    LGPU_E_NOT_D0 = -19,
    LGPU_E_PROVIDER_QUERY = -20
};

enum lgpu_bank {
    LGPU_BANK_SPM,
    LGPU_BANK_INFRA,
    LGPU_BANK_TOP,
    LGPU_BANK_APMIXED,
    LGPU_BANK_MFG,
    LGPU_BANK_GPU,
    LGPU_BANK_COUNT
};

enum lgpu_resource_type { LGPU_RESOURCE_MEMORY = 1, LGPU_RESOURCE_IRQ = 2 };
#define LGPU_RESOURCE_LEVEL 1u

/* An OS adapter translates native descriptors into this host-testable input. */
struct lgpu_resource {
    uint32_t type;
    uint32_t flags;
    uint64_t raw_start;
    uint64_t translated_start;
    uint64_t length;
};

struct lgpu_resources {
    uint64_t gpu_base;
    uint64_t mfg_base;
    uint32_t gpu_length;
    uint32_t mfg_length;
    uint32_t irq_mask;
    uint32_t irq_vector[LGPU_IRQ_COUNT];
};

/* Immutable property registers, not values inferred from the DT compatible. */
struct lgpu_raw_properties {
    uint32_t gpu_id;
    uint32_t mmu_features;
    uint32_t as_present;
    uint32_t js_present;
    uint64_t shader_present;
    uint64_t tiler_present;
    uint64_t l2_present;
    uint32_t l2_features;
    uint32_t mem_features;
    uint32_t coherency_features;
    uint32_t core_features;
};

struct lgpu_properties {
    uint32_t product_id;
    uint32_t architecture;
    uint32_t revision_major;
    uint32_t revision_minor;
    uint32_t va_bits;
    uint32_t pa_bits;
    uint32_t shader_count;
    uint32_t address_space_count;
    uint32_t job_slot_count;
    uint32_t reserved;
};

/* Always-on snapshot obtained while a board power owner pins the GPU rails,
 * clock parents, domains and interconnect. This structure alone is NOT a lease. */
struct lgpu_power_snapshot {
    uint32_t status;
    uint32_t status2;
    uint32_t mfg;
    uint32_t core0;
    uint32_t core1;
    uint32_t async;
    uint32_t protect;
    uint32_t protect_ack;
    uint32_t top;
    uint32_t pll_con0;
    uint32_t pll_con3;
    uint32_t reserved;
};

#define LGPU_POWER_DOMAIN_MASK 0x00007800u
#define LGPU_POWER_ON_BITS 0x0000000du /* RST_B, ON, ON_2ND */
#define LGPU_POWER_BAD_BITS 0x00001112u /* ISO, CLK_DIS, SRAM_PDN/ACK */
#define LGPU_BUS_PROTECT_MASK 0x02600000u

int lgpu_validate_resources(const struct lgpu_resource *input, size_t count,
                            struct lgpu_resources *output);
int lgpu_decode_properties(const struct lgpu_raw_properties *raw,
                           struct lgpu_properties *output);
int lgpu_validate_power(const struct lgpu_power_snapshot *snapshot);
int lgpu_validate_clock(uint32_t top, uint32_t con0, uint32_t con3);
const char *lgpu_status_name(int status);

#endif
