/* Actual MediaTek GPIO -> lancelot active-low volume-key regression.
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#undef NULL
#define MDEPKG_NDEBUG
#include <Uefi.h>
#include "Silicon/MediaTek/MediaTekPkg/Drivers/GpioDxe/GpioDxe.c"
#include "Silicon/MediaTek/MT6768Pkg/Library/GpioImplLib/GpioImplLib.c"
#include "Platforms/Xiaomi/lancelotPkg/Library/KeypadDeviceLib/KeypadDeviceLib.c"

EFI_GUID gMediaTekGpioProtocolGuid, gMediaTekPmicProtocolGuid;
static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS=&Services;
static UINT32 Registers[1024];
static UINTN LastOffset, Reads, KeyCount;
static EFI_TPL CurrentTpl=TPL_APPLICATION;
static EFI_KEY_DATA LastKey;

UINT32 EFIAPI MmioRead32(UINTN Address) {
  assert(Address>=0x10005000 && Address<0x10006000 && Address%4==0);
  LastOffset=Address-0x10005000; Reads++;
  return Registers[LastOffset/4];
}
UINT32 EFIAPI MmioWrite32(UINTN Address,UINT32 Value) {
  assert(0 && "key polling must not alter GPIO configuration"); return Value;
}
VOID *EFIAPI SetMem(VOID *Dest,UINTN Length,UINT8 Value) { return memset(Dest,Value,Length); }
EFI_STATUS EFIAPI LocateMemoryRegionByName(CHAR8 *Name,EFI_MEMORY_REGION_DESCRIPTOR *Region) {
  assert(!strcmp(Name,"Pinctrl")); memset(Region,0,sizeof(*Region)); Region->Address=0x10005000; return 0;
}
static VOID EFIAPI NoPmicKey(BOOLEAN *Pressed) { *Pressed=FALSE; }
static MTK_PMIC_PROTOCOL Pmic={.PowerButtonPressed=NoPmicKey,.HomeButtonPressed=NoPmicKey};
static EFI_STATUS EFIAPI Locate(EFI_GUID *Guid,VOID *Registration,VOID **Out) {
  if(Guid==&gMediaTekGpioProtocolGuid) *Out=&mGpio;
  else { assert(Guid==&gMediaTekPmicProtocolGuid); *Out=&Pmic; } return 0;
}
static EFI_TPL EFIAPI Raise(EFI_TPL Tpl) { EFI_TPL Old=CurrentTpl; CurrentTpl=Tpl; return Old; }
static VOID EFIAPI Restore(EFI_TPL Tpl) { CurrentTpl=Tpl; }
static VOID EFIAPI Push(KEYPAD_RETURN_API *This,EFI_KEY_DATA *Key) { KeyCount++; LastKey=*Key; }
static KEYPAD_RETURN_API ReturnApi={Push};
static void Tick(UINTN Count) {
  for(UINTN I=0;I<Count;I++) assert(KeypadDeviceGetKeys(NULL,&ReturnApi,10000000)==0);
}
int main(void) {
  Services.LocateProtocol=Locate; Services.RaiseTPL=Raise; Services.RestoreTPL=Restore;
  mPinctrlRegion.Address=0x10005000;
  assert(KeypadDeviceConstructor()==RETURN_SUCCESS);
  assert(KeypadDeviceReset(NULL)==0);
  // GPIO93 is an input (DIR bit 0), pulled high when released. Its unused DO
  // latch is low; it must not produce an active-low volume-down press.
  Registers[0x020/4]=0; Registers[0x120/4]=0; Registers[0x220/4]=1U<<29;
  Tick(200); assert(KeyCount==0 && LastOffset==0x220);
  // A real short active-low press still produces SCAN_DOWN on release.
  Registers[0x220/4]=0; Tick(5);
  Registers[0x220/4]=1U<<29; Tick(5);
  assert(KeyCount==1 && LastKey.Key.ScanCode==SCAN_DOWN);
  // A held physical key retains existing repeat behavior; release stops it.
  Registers[0x220/4]=0; Tick(80); assert(KeyCount>2);
  Registers[0x220/4]=1U<<29; Tick(5); UINTN Saved=KeyCount;
  Tick(200); assert(KeyCount==Saved && CurrentTpl==TPL_APPLICATION);
  // Output-mode reads retain output-latch semantics; direction bits are not
  // inverted or rewritten. Exercise the high bit without signed-shift UB.
  BOOLEAN State;
  Registers[0x020/4]=1U<<29; Registers[0x120/4]=1U<<29; Registers[0x220/4]=0;
  assert(GpioGetState(93,&State)==0 && State && LastOffset==0x120);
  Registers[0x000/4]=0; Registers[0x200/4]=1U<<31;
  assert(GpioGetState(31,&State)==0 && State && LastOffset==0x200);
  Saved=Reads; assert(GpioGetState(180,&State)==EFI_INVALID_PARAMETER && Reads==Saved);
  puts("PASS: actual GPIO/volume-key idle, press, hold/release, input/output register selection and bit31 (ASan/UBSan)");
  return 0;
}
