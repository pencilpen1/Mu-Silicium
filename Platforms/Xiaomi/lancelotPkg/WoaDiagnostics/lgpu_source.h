/* SPDX-License-Identifier: MIT -- observations never mint resource authority. */
#ifndef LGPU_SOURCE_H
#define LGPU_SOURCE_H
#if defined(LGPU_UEFI)
#include <Uefi.h>
typedef UINT8 lgfs_u8;
typedef UINT32 lgfs_u32;
typedef UINT64 lgfs_u64;
#else
#include <stdint.h>
typedef uint8_t lgfs_u8;
typedef uint32_t lgfs_u32;
typedef uint64_t lgfs_u64;
#endif
#define LGFS_VERSION 1u
#define LGFS_BOARD 0x6768u
#define LGFS_SIGNATURE 0x5253474cu /* ACPI bytes LGSR; no namespace device. */
#define LGFS_ACPI_PROVIDER 0x41435049u /* Windows provider 'ACPI'. */
#define LGFS_TABLE_BYTES 256u
#define LGFS_DXE 1u
#define LGFS_READY 2u
#define LGFS_EBS 3u
#define LGFS_WRITER_ATF 1u
#define LGFS_WRITER_SSPM 2u
#define LGFS_WRITER_AP_DVFS 4u
#define LGFS_WRITER_EEM 8u
#define LGFS_WRITER_FHCTL 16u
#define LGFS_WRITERS 31u
#define IOCTL_LGPU_SOURCE_STATUS ((0x22u<<16)|(1u<<14)|(0x813u<<2))
#pragma pack(push,1)
struct lgpu_firmware_record {
    lgfs_u32 signature, length;
    lgfs_u8 acpi_revision, checksum, oem_id[6], oem_table_id[8];
    lgfs_u32 oem_revision, creator_id, creator_revision;
    lgfs_u32 version, board_id, payload_bytes, phase;
    lgfs_u64 boot_id, record_generation, counter_frequency, ready_counter;
    lgfs_u32 observed_writers, quiesced_writers, drained_writers, authorized_writers;
    lgfs_u64 writer_generation, profile_generation, root_generation, restore_generation;
    lgfs_u8 baseline_commit[40], source_sha256[32], image_sha256[32], reserved[20];
};
#pragma pack(pop)
#if defined(LGPU_UEFI)
STATIC_ASSERT(sizeof(struct lgpu_firmware_record)==LGFS_TABLE_BYTES,"LGSR table 256");
#else
_Static_assert(sizeof(struct lgpu_firmware_record)==LGFS_TABLE_BYTES,"LGSR table 256");
enum lgfs_reason {
    LGFS_NONE, LGFS_NOT_REQUESTED, LGFS_TABLE_NOT_FOUND, LGFS_LENGTH, LGFS_HEADER,
    LGFS_CHECKSUM, LGFS_BOARD_MISMATCH, LGFS_VERSION_MISMATCH, LGFS_RESERVED,
    LGFS_BOOT_GENERATION, LGFS_COMMIT_MISMATCH, LGFS_PROVENANCE_MISSING,
    LGFS_WRITER_FIELDS, LGFS_TRANSPORT, LGFS_PENDING, LGFS_LATE, LGFS_LIFECYCLE
};
struct lgpu_source_query { uint32_t size, version, refresh, reserved; };
struct lgpu_source_status {
    uint32_t size, version, request_complete, read_only;
    int32_t transport_status;
    uint32_t query_executed, table_present, table_valid;
    uint32_t failure_reason, source_identity_valid, phase, firmware_authority_bound;
    uint64_t sampled_qpc_us, boot_id, record_generation;
    uint32_t observed_writers, quiesced_writers, drained_writers, authorized_writers;
    uint32_t code_measure_valid, checksum_valid, commit_match, pending_storage;
    uint8_t gpu_source_sha256[32], gpu_binary_sha256[32];
    struct lgpu_firmware_record record;
    uint32_t root_references, root_code_held, root_in_call, root_query_count;
    uint32_t observer_references, code_failures;
};
_Static_assert(sizeof(struct lgpu_source_status)==448,"source observation ABI1/448");
unsigned lgfs_check_record(const struct lgpu_firmware_record *, uint32_t bytes);
const char *lgfs_reason_name(unsigned);
void lgfs_sha256(const void *, uint32_t, uint8_t digest[32]);
/* Clock calculations use CONFIGURATION observations only. They perform no
 * MMIO and establish neither physical rate nor a source/restore lease. */
struct lgpu_clock_observation {
    uint32_t parent, gated, pll_enabled, pll_powered;
    uint32_t pcw, post_divider, reference_hz, configured_hz;
    uint32_t restore_parent_mask, input_valid, configuration_valid, reserved;
};
int32_t lgfs_clock_decode(uint32_t cfg1,uint32_t con0,uint32_t con1,uint32_t con3,
                          uint32_t reference_hz,struct lgpu_clock_observation *);
#endif
#endif
