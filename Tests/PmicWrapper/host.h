/* RAM-only UEFI doubles. Actual Mu source is included by each test. */
#ifndef MU_PWRAP_HOST_H
#define MU_PWRAP_HOST_H
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#define STATIC static
#define CONST const
#define IN
#define OUT
#define OPTIONAL
#define EFIAPI
#define TRUE true
#define FALSE false
#define DEBUG(args) ((void)0)
#define EFI_D_ERROR 1
#define BIT0 1u
#define BIT1 2u
#define BIT3 8u
#define BIT19 (1u << 19)
#define BIT20 (1u << 20)
#define BIT21 (1u << 21)
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef void VOID;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uintptr_t UINTN;
typedef uint64_t EFI_STATUS;
typedef bool BOOLEAN;
typedef char CHAR8;
typedef void *EFI_HANDLE;
typedef struct { int unused; } EFI_SYSTEM_TABLE;
typedef struct { uint32_t a; uint16_t b,c; uint8_t d[8]; } EFI_GUID;
#define EFI_SUCCESS UINT64_C(0)
#define EFI_INVALID_PARAMETER (UINT64_C(1)<<63|2)
#define EFI_UNSUPPORTED (UINT64_C(1)<<63|3)
#define EFI_NOT_READY (UINT64_C(1)<<63|6)
#define EFI_DEVICE_ERROR (UINT64_C(1)<<63|7)
#define EFI_ACCESS_DENIED (UINT64_C(1)<<63|15)
#define EFI_TIMEOUT (UINT64_C(1)<<63|18)
#define EFI_ERROR(s) (((s)>>63)!=0)
#define TPL_NOTIFY 16
typedef struct { BOOLEAN held; } EFI_LOCK;
#define EFI_INITIALIZE_LOCK_VARIABLE(tpl) {false}
typedef struct { UINTN Address; UINTN Length; } EFI_MEMORY_REGION_DESCRIPTOR;
typedef struct {
    EFI_STATUS (*InstallMultipleProtocolInterfaces)(EFI_HANDLE *, ...);
    EFI_STATUS (*LocateProtocol)(EFI_GUID *, VOID *, VOID **);
} EFI_BOOT_SERVICES;
extern EFI_BOOT_SERVICES *gBS;
EFI_STATUS EfiAcquireLockOrFail(EFI_LOCK *);
VOID EfiReleaseLock(EFI_LOCK *);
UINT64 GetPerformanceCounter(void);
UINT64 GetTimeInNanoSecond(UINT64);
VOID MicroSecondDelay(UINTN);
UINT32 MmioRead32(UINTN);
VOID MmioWrite32(UINTN,UINT32);
EFI_STATUS LocateMemoryRegionByName(CONST CHAR8 *,EFI_MEMORY_REGION_DESCRIPTOR *);
static inline int AsciiStrCmp(CONST CHAR8 *a,CONST CHAR8 *b) { return strcmp(a,b); }
#endif
