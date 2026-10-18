/** Host test of the real MemoryPeim map/HOB output; MMU calls are recorded. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#undef NULL
#define _PCD_GET_MODE_BOOL_PcdPrePiProduceMemoryTypeInformationHob FALSE
#include "ws/MT6768Pkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c"

VOID *gFdtBase;
static UINTN Resources, Allocations, Mappings;
static BOOLEAN SawFramebuffer, SawPool, SawLog;
static UINT64 LastEnd;
static struct { UINT64 Base, Size; EFI_RESOURCE_TYPE Type; } Resource[128];

VOID *EFIAPI ZeroMem (VOID *P, UINTN N) { return memset(P, 0, N); }
VOID *EFIAPI CopyMem (VOID *D, CONST VOID *S, UINTN N) { return memcpy(D, S, N); }
UINTN EFIAPI AsciiStrLen (CONST CHAR8 *S) { return strlen(S); }
INTN EFIAPI AsciiStrCmp (CONST CHAR8 *A, CONST CHAR8 *B) { return strcmp(A, B); }
INTN EFIAPI AsciiStrnCmp (CONST CHAR8 *A, CONST CHAR8 *B, UINTN N) { return strncmp(A, B, N); }
BOOLEAN EFIAPI DebugPrintEnabled (VOID) { return FALSE; }
BOOLEAN EFIAPI DebugPrintLevelEnabled (CONST UINTN Level) { return FALSE; }
VOID EFIAPI DebugPrint (UINTN Level, CONST CHAR8 *Format, ...) {}
VOID BuildMemoryTypeInformationHob (VOID) {}

VOID EFIAPI BuildResourceDescriptorHob (EFI_RESOURCE_TYPE Type, EFI_RESOURCE_ATTRIBUTE_TYPE Attr, EFI_PHYSICAL_ADDRESS Base, UINT64 Size)
{
  assert(Size && Base == LastEnd);
  assert(!(Base & 4095) && !(Size & 4095));
  LastEnd = Base + Size;
  if (Base == UEFI_POOL_BASE) {
    assert(Type == EFI_RESOURCE_SYSTEM_MEMORY && Size == UEFI_POOL_SIZE);
    SawPool = TRUE;
  }
  if (Base == FB_BASE) {
    assert(Type == EFI_RESOURCE_MEMORY_RESERVED && Size == FB_SIZE);
    SawFramebuffer = TRUE;
  }
  if (Base == LANCELOT_LOG_BASE) {
    assert(Type == EFI_RESOURCE_SYSTEM_MEMORY && Size == LANCELOT_LOG_SIZE);
    SawLog = TRUE;
  }
  Resource[Resources].Base = Base;
  Resource[Resources].Size = Size;
  Resource[Resources].Type = Type;
  Resources++;
}

VOID EFIAPI BuildMemoryAllocationHob (EFI_PHYSICAL_ADDRESS Base, UINT64 Size, EFI_MEMORY_TYPE Type)
{
  // The pool and conventional RAM must not be marked already allocated.
  // The stack is allocated once by PrePi's separate BuildStackHob call.
  assert(Type != EfiConventionalMemory);
  if (Base == LANCELOT_LOG_BASE) {
    assert(Size == LANCELOT_LOG_SIZE && Type == EfiReservedMemoryType);
  } else {
    assert(Base == UEFI_FD_BASE || Base == UEFI_VECTORS_BASE);
    assert(Size == (Base == UEFI_FD_BASE ? UEFI_FD_SIZE : UEFI_VECTORS_SIZE));
  }
  Allocations++;
}

EFI_STATUS EFIAPI ArmConfigureMmu (ARM_MEMORY_REGION_DESCRIPTOR *Map, VOID **Table, UINTN *Size)
{
  UINT64 End = 0;
  while (Map->Length) {
    assert(Map->PhysicalBase == Map->VirtualBase && Map->PhysicalBase >= End);
    assert(!(Map->PhysicalBase < 0x77000000ULL && Map->PhysicalBase + Map->Length > 0x70000000ULL));
    assert(!(Map->Length & 4095));
    BOOLEAN Match = FALSE;
    for (UINTN I = 0; I < Resources; I++) {
      if (Resource[I].Base == Map->PhysicalBase && Resource[I].Size == Map->Length) {
        assert(Resource[I].Type != EFI_RESOURCE_MEMORY_RESERVED || Resource[I].Base == FB_BASE);
        Match = TRUE;
      }
    }
    assert(Match);
    End = Map->PhysicalBase + Map->Length;
    Map++;
    Mappings++;
    assert(Mappings < MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT);
  }
  assert(End <= 0x140000000ULL);
  *Table = NULL;
  *Size = 0;
  return EFI_SUCCESS;
}

int main(int argc, char **argv)
{
  assert(argc == 2);
  FILE *F = fopen(argv[1], "rb"); assert(F);
  fseek(F, 0, SEEK_END); long N = ftell(F); rewind(F);
  gFdtBase = malloc(N); assert(gFdtBase);
  assert(fread(gFdtBase, 1, N, F) == (size_t)N); fclose(F);
  assert(MemoryPeim(UEFI_POOL_BASE, UEFI_POOL_SIZE) == EFI_SUCCESS);
  assert(SawPool && SawFramebuffer && SawLog && Allocations == 3 && Mappings < Resources);
  assert(LastEnd == 0x140000000ULL);
  printf("PASS: runtime MemoryPeim: %zu resources, %zu allocations, %zu mappings\n", (size_t)Resources, (size_t)Allocations, (size_t)Mappings);
  free(gFdtBase);
  return 0;
}
