/* Included after the original packet fixtures by test_lancelot_musb.py.
 * Tests the actual managed Device entry/start/stop code, not replacement hooks.
 * SPDX-License-Identifier: GPL-2.0-only
 */
EFI_GUID gMediaTekUsbRoleProtocolGuid;
static EFI_BOOT_SERVICES DeviceServices;
static MTK_USB_DEVICE_START SavedStart;
static MTK_USB_DEVICE_STOP SavedStop;
static UINTN DeviceEventCount;
static BOOLEAN DeviceTimerRunning;
static EFI_EVENT_NOTIFY DeviceExitCallback;

static EFI_STATUS EFIAPI SaveCallbacks(MTK_USB_ROLE_PROTOCOL *This,
    MTK_USB_DEVICE_START On, MTK_USB_DEVICE_STOP Off, VOID *Context) {
  SavedStart=On; SavedStop=Off; return EFI_SUCCESS;
}
static MTK_USB_ROLE_PROTOCOL RoleProtocol={SaveCallbacks};
static EFI_STATUS EFIAPI DeviceLocate(EFI_GUID *Guid,VOID *Registration,VOID **Out) {
  assert(Guid==&gMediaTekUsbRoleProtocolGuid); *Out=&RoleProtocol; return 0;
}
static EFI_STATUS EFIAPI DeviceCreate(UINT32 Type,EFI_TPL Tpl,EFI_EVENT_NOTIFY Callback,VOID *Context,EFI_EVENT *Event) {
  assert(Tpl==TPL_NOTIFY); *Event=(EFI_EVENT)(UINTN)++DeviceEventCount; return 0;
}
static EFI_STATUS EFIAPI DeviceCreateEx(UINT32 Type,EFI_TPL Tpl,EFI_EVENT_NOTIFY Callback,
    CONST VOID *Context,CONST EFI_GUID *Group,EFI_EVENT *Event) {
  DeviceExitCallback=Callback; *Event=(EFI_EVENT)(UINTN)++DeviceEventCount; return 0;
}
static EFI_STATUS EFIAPI DeviceTimer(EFI_EVENT Event,EFI_TIMER_DELAY Type,UINT64 Trigger) {
  DeviceTimerRunning=Type==TimerPeriodic; return 0;
}
static EFI_STATUS EFIAPI DeviceClose(EFI_EVENT Event) { return 0; }

static void EnumerateAndQuery(void) {
  Request(0,5,17,0,0); Csr(0,0); PollControl(TRUE);
  Request(0,9,1,0,0); Csr(0,0); PollControl(TRUE);
  Bulk("getvar:product");
  assert(OutputLength[1]==17 && !memcmp(Output[1],"OKAYlancelot-uefi",17));
}

int main(int argc,char **argv) {
  RegressionMain(argc,argv);
  DeviceServices.CreateEvent=DeviceCreate;
  DeviceServices.CreateEventEx=DeviceCreateEx;
  DeviceServices.SetTimer=DeviceTimer;
  DeviceServices.CloseEvent=DeviceClose;
  DeviceServices.LocateProtocol=DeviceLocate;
  gBS=&DeviceServices;
  mStarted=mActive=FALSE; mStopped=TRUE;
  HardwareForbidden=TRUE;
  assert(MusbDiagnosticEntry(NULL,NULL)==0);
  assert(SavedStart && SavedStop && !DeviceTimerRunning);
  HardwareForbidden=FALSE;
  UINT16 Version=0xE503; memcpy(Reg+0x6C,&Version,2);
  UINT64 Written=LOG->Written;
  assert(SavedStart(NULL)==0 && mStarted && mActive && !mStopped && DeviceTimerRunning);
  assert(LOG->Written==Written); // managed restart must not clear the shared boot log
  EnumerateAndQuery();
  SavedStop(NULL);
  assert(!mActive && mStopped && !mStarted && !DeviceTimerRunning && Reg[POWER]==0);
  HardwareForbidden=TRUE;
  Poll(NULL,NULL); SavedStop(NULL); // Host ownership: even a stale device callback does no MMIO
  HardwareForbidden=FALSE;
  // Simulate the Host modifying the indexed state/FIFOs, then returning ownership.
  memset(Ep,0xAA,sizeof(Ep));
  assert(SavedStart(NULL)==0 && !mStopped && DeviceTimerRunning);
  EnumerateAndQuery();
  DeviceExitCallback(NULL,NULL);
  assert(!mActive && !mStarted && mStopped && !DeviceTimerRunning);
  HardwareForbidden=TRUE; SavedStop(NULL); Poll(NULL,NULL);
  puts("PASS: actual managed Device registration, stop/restart, getvar after Host handoff, shared log and EBS");
  return 0;
}
