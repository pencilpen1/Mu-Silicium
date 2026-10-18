/** Host tests of the actual shared RAM ring. SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#undef NULL
#include <Uefi.h>
#include <Configuration/BootLogConfig.h>

static union { UINT64 Align; UINT8 Bytes[LANCELOT_LOG_SIZE]; } LogMemory;
#define BOOT_LOG_ADDRESS ((UINTN)LogMemory.Bytes)
#include "ws/MT6768Pkg/Library/BootLogLib/BootLogLib.c"

static BOOLEAN Irq = TRUE;
BOOLEAN EFIAPI ArmGetInterruptState (VOID) { return Irq; }
UINTN EFIAPI ArmDisableInterrupts (VOID) { UINTN Old=Irq; Irq=FALSE; return Old; }
VOID EFIAPI ArmEnableInterrupts (VOID) { Irq = TRUE; }
VOID EFIAPI MemoryFence (VOID) {}

int main(void)
{
  UINT64 Id, Start;
  UINT8 Out[256];
  BootLogWrite("ignored before init",19);
  assert(!LOG->Written && Irq);
  BootLogInitialize(0x1234);
  BootLogWrite("SEC\n",4);
  BootLogWrite("DXE\n",4);
  assert(BootLogSnapshot(0,&Id,&Start,Out,sizeof(Out))==8);
  assert(Id==0x1234 && Start==0 && !memcmp(Out,"SEC\nDXE\n",8) && Irq);
  assert(BootLogSnapshot(4,&Id,&Start,Out,2)==2 && Start==4 && !memcmp(Out,"DX",2));
  assert(BootLogSnapshot(8,&Id,&Start,Out,sizeof(Out))==0 && Start==8);

  // Multi-image callers share state; writes cannot wait/recurse inside a fault.
  LOG->Busy=1;
  BootLogWrite("drop",4);
  assert(LOG->Written==8 && Irq);
  LOG->Busy=0;
  Irq=FALSE;
  BootLogWrite("irq-off",7);
  BootLogSnapshot(0,&Id,&Start,Out,sizeof(Out));
  assert(!Irq);
  Irq=TRUE;

  UINTN N=LOG_CAPACITY+71;
  UINT8 *Large=malloc(N), *All=malloc(LOG_CAPACITY);
  assert(Large && All);
  for(UINTN I=0;I<N;I++) Large[I]=(UINT8)I;
  BootLogInitialize(99);
  BootLogWrite(Large,N);
  assert(LOG->Written==N);
  assert(BootLogSnapshot(0,&Id,&Start,All,LOG_CAPACITY)==LOG_CAPACITY);
  assert(Id==99 && Start==71 && !memcmp(All,Large+71,LOG_CAPACITY));
  BootLogWrite("wrapped\0bytes",13);
  assert(BootLogSnapshot(N-3,&Id,&Start,Out,sizeof(Out))==16);
  assert(Start==N-3 && !memcmp(Out,Large+N-3,3) && !memcmp(Out+3,"wrapped\0bytes",13));
  BootLogSnapshot(MAX_UINT64,&Id,&Start,Out,sizeof(Out));
  assert(Start==84); // old host cursor from another boot clamps to oldest
  BootLogSetUsbActive(TRUE); assert(BootLogUsbActive());
  BootLogInitialize(100); assert(!BootLogUsbActive());
  assert(BootLogSnapshot(0,&Id,&Start,Out,sizeof(Out))==0 && Id==100 && Start==0);
  LOG->Written=MAX_UINT64-1;
  BootLogWrite("overflow",8);
  assert(LOG->Written==MAX_UINT64-1 && !LOG->Busy && Irq);
  free(Large); free(All);
  puts("PASS: shared boot log, cursor reads, binary bytes, ring overflow/wrap, IRQ restore, reentrancy and reboot reset (ASan/UBSan)");
}
