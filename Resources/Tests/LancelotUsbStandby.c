/* Exercise the actual no-OS BDS hook beyond its former ten-second shutdown.
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>
#undef NULL
#include <Uefi.h>
#include "Platforms/Xiaomi/lancelotPkg/Library/UsbBootManagerLib/UsbBootManager.c"

static EFI_BOOT_SERVICES Services;
static EFI_SYSTEM_TABLE SystemTable;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input;
EFI_BOOT_SERVICES *gBS=&Services;
EFI_SYSTEM_TABLE *gST=&SystemTable;
static jmp_buf Done;
static UINTN Ticks, BootCalls, Frees, Rescans, Refreshes, Prints, WatchdogDisables;
static UINTN LastReadTick;
static BOOLEAN FailTimer;

UINTN EFIAPI Print(CONST CHAR16 *Format,...) { Prints++; return 0; }
EFI_STATUS EFIAPI EfiBootManagerUpdateConsoleVariable(CONSOLE_TYPE Type,
    EFI_DEVICE_PATH_PROTOCOL *Add,EFI_DEVICE_PATH_PROTOCOL *Remove) { return EFI_SUCCESS; }
VOID EFIAPI EfiBootManagerConnectAll(VOID) { Rescans++; }
VOID EFIAPI EfiBootManagerRefreshAllBootOption(VOID) { Refreshes++; }
EFI_STATUS EFIAPI EfiBootManagerGetBootManagerMenu(EFI_BOOT_MANAGER_LOAD_OPTION *Option) {
  memset(Option,0,sizeof(*Option)); return EFI_SUCCESS;
}
VOID EFIAPI EfiBootManagerBoot(EFI_BOOT_MANAGER_LOAD_OPTION *Option) { BootCalls++; }
EFI_STATUS EFIAPI EfiBootManagerFreeLoadOption(EFI_BOOT_MANAGER_LOAD_OPTION *Option) {
  Frees++; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI DisableWatchdog(UINTN Timeout,UINT64 Code,UINTN Size,CHAR16 *Data) {
  assert(Timeout==0); WatchdogDisables++; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Create(UINT32 Type,EFI_TPL Tpl,EFI_EVENT_NOTIFY Fn,VOID *Context,EFI_EVENT *Event) {
  assert(Type==EVT_TIMER && Tpl==TPL_APPLICATION && Fn==NULL);
  if(FailTimer) return EFI_OUT_OF_RESOURCES;
  *Event=(EFI_EVENT)(UINTN)1; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Timer(EFI_EVENT Event,EFI_TIMER_DELAY Type,UINT64 Trigger) {
  assert(Type==TimerPeriodic && Trigger==1000000); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Close(EFI_EVENT Event) { return EFI_SUCCESS; }
static void Advance(void) {
  // 130 * 100 ms of yielding, including input/menu/rescan after ten seconds.
  if(++Ticks==130) longjmp(Done,1);
}
static EFI_STATUS EFIAPI Wait(UINTN Count,EFI_EVENT *Events,UINTN *Index) {
  assert(Count==1 && !FailTimer); Advance(); *Index=0; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Stall(UINTN Us) {
  assert(Us==100000 && FailTimer); Advance(); return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI ReadKey(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,EFI_INPUT_KEY *Key) {
  if(LastReadTick==Ticks) return EFI_NOT_READY;
  LastReadTick=Ticks;
  if(Ticks==110) { Key->ScanCode=SCAN_NULL; Key->UnicodeChar='a'; }
  else if(Ticks==120) { Key->ScanCode=SCAN_F1; Key->UnicodeChar=0; }
  else if(Ticks==125) { Key->ScanCode=SCAN_F2; Key->UnicodeChar=0; }
  else { Key->ScanCode=SCAN_DOWN; Key->UnicodeChar=0; } // repeated key must not flood standby output
  return EFI_SUCCESS;
}
int main(void) {
  Services.CreateEvent=Create; Services.SetTimer=Timer; Services.CloseEvent=Close;
  Services.WaitForEvent=Wait; Services.Stall=Stall; Services.SetWatchdogTimer=DisableWatchdog;
  Input.ReadKeyStroke=ReadKey; SystemTable.ConIn=&Input;
  for(int Pass=0;Pass<2;Pass++) {
    FailTimer=Pass!=0; Ticks=LastReadTick=0;
    BootCalls=Frees=Rescans=Refreshes=Prints=WatchdogDisables=0;
    if(setjmp(Done)==0) { DeviceBootManagerUnableToBoot(); assert(0 && "must not return to shutdown policy"); }
    assert(Ticks==130 && BootCalls==1 && Frees==1 && Rescans==1 && Refreshes==1);
    assert(Prints==3 && WatchdogDisables==2); // two banners and one explicit rescan message
  }
  puts("PASS: no-OS USB standby yields beyond 10 seconds, keyboard/F1/F2 handling and timer-allocation fallback");
  return 0;
}
