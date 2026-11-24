/* SPDX-License-Identifier: MIT */
#ifndef LGPU_MMU_H
#define LGPU_MMU_H
#include "lgpu_hw.h"

#define LGPU_PAGE_SIZE 4096u
#define LGPU_PTES_PER_TABLE 512u
#define LGPU_VA_LIMIT (UINT64_C(1) << 35)
#define LGPU_PA_LIMIT (UINT64_C(1) << 40)
#define LGPU_PA_MASK UINT64_C(0x000000fffffff000)
#define LGPU_PTE_INVALID UINT64_C(2)
#define LGPU_MAX_TABLES 1024u
#define LGPU_MAX_MAP_PAGES 65536u
#define LGPU_MAP_READ 1u
#define LGPU_MAP_WRITE 2u
#define LGPU_MAP_EXECUTE 4u

/* G52 profile: 35-bit three-level VA, uncached table walks. The BOOT54 private
 * job backend programs it only after reading the exact live MMU/ID profile.
 * Attribute zero is GPU cached; real explicit CPU/GPU cache handoff is needed. */
#define LGPU_G52_TRANSCFG UINT64_C(0x01000506)
#define LGPU_G52_MEMATTR0 UINT64_C(0x88)

struct lgpu_table {
    uint64_t *entries; /* Caller-provided, page-aligned, CPU writable backing. */
    uint64_t physical;
    uint32_t used;
    uint32_t dirty;
    uint16_t level;
    uint16_t parent;
    uint16_t parent_slot;
    uint16_t reserved;
};

struct lgpu_vm {
    struct lgpu_table *tables;
    uint32_t table_count;
    uint32_t allocated_tables;
    uint32_t mapped_pages;
    uint32_t initialized;
};

/* Offline page-table builder: caller serializes all operations. None of these
 * functions may be used on tables attached to a GPU. There is no DMA allocation,
 * AS attachment or cache/TLB operation inside THIS builder. lgpu_native_job.c
 * supplies a separate bounded transaction; it never edits an attached table.
 * The caller owns the table pool and every mapped backing page. */
int lgpu_vm_init(struct lgpu_vm *vm, struct lgpu_table *tables, uint32_t count);
int lgpu_vm_map(struct lgpu_vm *vm, uint64_t va, const uint64_t *physical,
                uint32_t count, uint32_t flags);
int lgpu_vm_unmap(struct lgpu_vm *vm, uint64_t va, uint32_t count);
int lgpu_vm_translate(struct lgpu_vm *vm, uint64_t va, uint64_t *physical,
                      uint32_t *flags);
int lgpu_pte_encode(uint64_t physical, uint32_t flags, uint64_t *entry);
#endif
