/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uintptr_t UINTN;
typedef int BOOLEAN;
typedef void VOID;
typedef struct { UINT64 Address, Length; } EFI_MEMORY_REGION_DESCRIPTOR;
#define TRUE 1
#define FALSE 0
#define BIT8 (1u << 8)
#define BIT28 (1u << 28)
#define EFI_ERROR(x) ((x) != 0)
static UINT32 regs[2][1024], writes;
static EFI_MEMORY_REGION_DESCRIPTOR fb = {0x7cdb0000, 0x2250000};
static UINT32 *reg(UINTN address) {
  unsigned bank = address >= 0x1400c000;
  UINTN base = bank ? 0x1400c000 : 0x1400b000;
  assert(address >= base && address < base + 4096 && (address & 3) == 0);
  return &regs[bank][(address - base) / 4];
}
static UINT32 MmioRead32(UINTN address) { return *reg(address); }
static void MmioWrite32(UINTN address, UINT32 value) { *reg(address) = value; ++writes; }
static void BootLogPrint(const char *format, ...) { (void)format; }
static int LocateMemoryRegionByName(const char *name, EFI_MEMORY_REGION_DESCRIPTOR *out) {
  if (strcmp(name, "Display Reserved") == 0) *out = fb;
  else if (strcmp(name, "Display OVL") == 0) *out = (EFI_MEMORY_REGION_DESCRIPTOR){0x1400b000,4096};
  else if (strcmp(name, "Display OVL 2L") == 0) *out = (EFI_MEMORY_REGION_DESCRIPTOR){0x1400c000,4096};
  else return 1;
  return 0;
}
#include "display-under-test.inc"
static void reset(void) { memset(regs, 0, sizeof(regs)); writes = 0; }
static void surface(unsigned bank, unsigned layer, int extended) {
  unsigned con = (extended ? 0x330 : 0x30) + 0x20 * layer;
  unsigned addr = extended ? 0xfb0 + 4 * layer : 0xf40 + 0x20 * layer;
  regs[bank][(extended ? 0x324 : 0x2c) / 4] |= 1u << layer;
  regs[bank][addr / 4] = (UINT32)fb.Address;
  regs[bank][con / 4] = 0x01200100;
  regs[bank][(con + 0x14) / 4] = 0x80001100;
  regs[bank][(con + 8) / 4] = (2340u << 16) | 1080;
}
static void opaque(unsigned bank, unsigned con) {
  assert((regs[bank][con / 4] & 0x1ff) == 0xff);
  assert(regs[bank][con / 4] == 0x012000ff);
  assert(regs[bank][(con + 0x14) / 4] == 0x90001100);
  assert(writes == 2);
}
int main(void) {
  reset(); surface(0,3,0); EnableConstantBlending(); opaque(0,0x90);
  assert(regs[0][0x30/4] == 0);
  puts("PASS: active layer 3 is selected; inactive layer 0 stays unchanged");
  reset(); surface(0,1,0); surface(0,3,0);
  regs[0][0xf60/4] = 0x12340000;
  EnableConstantBlending(); opaque(0,0x90);
  assert(regs[0][0x50/4] == 0x01200100);
  puts("PASS: an unrelated active overlay is never changed");
  reset(); surface(1,1,0); EnableConstantBlending(); opaque(1,0x50);
  puts("PASS: matching two-layer engine is used when the first engine has no match");
  reset(); surface(0,2,1); EnableConstantBlending(); opaque(0,0x370);
  puts("PASS: extended layer uses its independent address register");
  reset(); surface(0,0,0); regs[0][0x44/4] = 0x1000;
  EnableConstantBlending(); assert(writes == 0);
  reset(); surface(0,0,0); regs[0][0x38/4] = (2340u<<16)|720;
  EnableConstantBlending(); assert(writes == 0);
  reset(); surface(0,0,0); regs[0][0x30/4] |= 1u << 28;
  EnableConstantBlending(); assert(writes == 0);
  reset(); EnableConstantBlending(); assert(writes == 0);
  puts("PASS: wrong geometry, stride, source type and disabled layers cannot authorize writes");
  return 0;
}
