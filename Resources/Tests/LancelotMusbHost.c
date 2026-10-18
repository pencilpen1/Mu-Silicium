/* Host-side scripted bus tests of the actual UEFI MUSB host controller.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#undef NULL
#include <Uefi.h>
static UINTN ScreenWrites;
VOID UsbStatusInitialize(VOID) { }
VOID UsbStatusUpdate(CONST CHAR8 *State, CONST CHAR8 *Detail) {
  assert(strlen(State)<=43 && strlen(Detail)<=43); ScreenWrites+=2;
}
#include "Silicon/MediaTek/MediaTekPkg/Drivers/MusbHostDxe/MusbHost.c"

EFI_GUID gEfiUsb2HcProtocolGuid, gEfiDevicePathProtocolGuid, gEfiEventExitBootServicesGuid;
EFI_GUID gMediaTekUsbRoleProtocolGuid;
static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS = &Services;
static EFI_TPL CurrentTpl = TPL_APPLICATION;
static UINT64 TimeNs;
static UINTN Accesses, PowerEnables;
static BOOLEAN Sink = TRUE, Vbus;
static BOOLEAN PcConnected, DeviceOwned;
static UINTN DeviceStarts, DeviceStops, HostPublished, RolePublished;
static UINT8 ConnectorEvents;
static EFI_STATUS RoleReadStatus;
static BOOLEAN FailPowerOff;
static EFI_STATUS BoardInitResult;
static UINTN Published, EventCount;
static UINT8 Regs[4096], Eps[16][16], Selected;
static UINT8 Tx[16][512], Rx[16][512];
static UINTN TxLen[16], RxPos[16];
typedef struct {
  UINT8 Ep;
  BOOLEAN In;
  UINT16 Command;
  UINTN Length;
  const UINT8 *Bytes;
  UINT16 Failure;
  BOOLEAN RetryNak;
  BOOLEAN Timeout;
  BOOLEAN NoReply;
} PACKET;
static PACKET Script[32];
static UINTN ScriptCount, Next;
static int Active[16], Delay[16];
static BOOLEAN NakDone[16];

static UINT8 *Map(UINTN Address) {
  assert(!DeviceOwned); // all Host MAC reads/writes forbidden during Device ownership
  assert(Address >= MUSB_BASE && Address < MUSB_BASE + sizeof(Regs));
  UINTN R = Address - MUSB_BASE;
  if (R >= 0x10 && R < 0x20) return &Eps[Selected][R - 0x10];
  return &Regs[R];
}
static UINT16 Load16(void *P) { UINT16 V; memcpy(&V,P,2); return V; }
static VOID Store16(void *P, UINT16 V) { memcpy(P,&V,2); }
static UINT32 Load32(void *P) { UINT32 V; memcpy(&V,P,4); return V; }
static VOID Store32(void *P, UINT32 V) { memcpy(P,&V,4); }
UINT64 EFIAPI GetPerformanceCounter(VOID) { return TimeNs; }
UINT64 EFIAPI GetTimeInNanoSecond(UINT64 Ticks) { return Ticks; }
UINTN EFIAPI MicroSecondDelay(UINTN Us) { TimeNs += Us * 1000; return Us; }
VOID EFIAPI MemoryFence(VOID) {}
VOID *EFIAPI CopyMem(VOID *D, CONST VOID *S, UINTN N) { return memmove(D,S,N); }
VOID *EFIAPI ZeroMem(VOID *D, UINTN N) { return memset(D,0,N); }
VOID EFIAPI BootLogInitialize(UINT64 Id) {}
VOID EFIAPI BootLogPrint(CONST CHAR8 *Format, ...) {}
UINTN EFIAPI AsciiSPrint(CHAR8 *Buffer, UINTN Size, CONST CHAR8 *Format, ...) {
  char Fmt[256]; UINTN N=0;
  for(UINTN I=0;Format[I];I++) {
    if(Format[I]=='%' && Format[I+1]=='a') { Fmt[N++]='%'; Fmt[N++]='s'; I++; }
    else if(Format[I]=='%' && Format[I+1]=='r') { Fmt[N++]='%'; Fmt[N++]='l'; Fmt[N++]='x'; I++; }
    else Fmt[N++]=Format[I];
  }
  Fmt[N]=0;
  va_list Args; va_start(Args,Format);
  int Result=vsnprintf(Buffer,Size,Fmt,Args); va_end(Args);
  return Result<0 ? 0 : (UINTN)Result;
}
EFI_STATUS LancelotUsbBoardInit(VOID) { return BoardInitResult; }
EFI_STATUS LancelotUsbPartnerIsSink(BOOLEAN *Out) { *Out=Sink; return EFI_SUCCESS; }
EFI_STATUS LancelotUsbGetRole(MTK_USB_ROLE *Role, UINT8 *Events, UINT8 *RawStatus) {
  if(EFI_ERROR(RoleReadStatus)) return RoleReadStatus;
  *Role=Sink ? MtkUsbRoleHost : PcConnected ? MtkUsbRoleDevice : MtkUsbRoleNone;
  *RawStatus=Sink ? 4 : PcConnected ? 0x88 : 0;
  *Events=ConnectorEvents; ConnectorEvents=0; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI DeviceOn(VOID *Context) {
  assert(!Vbus && mHost.ActiveRole==MtkUsbRoleDevice && !DeviceOwned);
  DeviceOwned=TRUE; DeviceStarts++; return EFI_SUCCESS;
}
static VOID EFIAPI DeviceOff(VOID *Context) { DeviceOwned=FALSE; DeviceStops++; }
EFI_STATUS LancelotUsbVbus(BOOLEAN Enable) {
  if (Enable) { assert(Sink); PowerEnables++; }
  else if(FailPowerOff) return EFI_DEVICE_ERROR;
  Vbus=Enable; return EFI_SUCCESS;
}
VOID LancelotUsbBoardExit(VOID) { Vbus=FALSE; }

static void Complete(UINT8 Ep) {
  if (Active[Ep] < 0) return;
  PACKET *P = &Script[Active[Ep]];
  if (P->Timeout || Delay[Ep]-- > 0) return;
  if (P->NoReply) { Store16(&Eps[Ep][6],0); Active[Ep]=-1; return; }
  UINTN Offset=P->In && Ep ? 6 : 2;
  if (P->RetryNak && !NakDone[Ep]) {
    Store16(&Eps[Ep][Offset], Ep==0 ? CSR0_NAK : P->In ? RX_NAK : TX_NAK);
    NakDone[Ep]=TRUE;
    return;
  }
  if (P->Failure) {
    Store16(&Eps[Ep][Offset],P->Failure); Active[Ep]=-1; return;
  }
  if (P->In) {
    if (P->Length) memcpy(Rx[Ep],P->Bytes,P->Length);
    RxPos[Ep]=0;
    Store16(&Eps[Ep][8],P->Length);
    Store16(&Eps[Ep][Offset],RX_READY);
  } else Store16(&Eps[Ep][Offset],Ep ? TX_MODE : 0);
  UINTN ToggleReg=P->In ? REG_RXTOG : REG_TXTOG;
  Store32(&Regs[ToggleReg],Load32(&Regs[ToggleReg])^(1U<<Ep));
  UINTN Irq=P->In && Ep ? REG_INTRRX : REG_INTRTX;
  Store16(&Regs[Irq],Load16(&Regs[Irq])|(1U<<Ep));
  Active[Ep]=-1;
}
UINT8 EFIAPI MmioRead8(UINTN Address) {
  assert(!DeviceOwned);
  Accesses++;
  if (Address>=MUSB_BASE+0x20 && Address<MUSB_BASE+0x40) {
    UINTN Ep=(Address-MUSB_BASE-0x20)/4;
    assert(RxPos[Ep]<512); return Rx[Ep][RxPos[Ep]++];
  }
  return *Map(Address);
}
UINT16 EFIAPI MmioRead16(UINTN Address) {
  Accesses++;
  if (Address==MUSB_BASE+REG_TXCSR || Address==MUSB_BASE+REG_RXCSR) Complete(Selected);
  return Load16(Map(Address));
}
UINT32 EFIAPI MmioRead32(UINTN Address) {
  assert(!DeviceOwned);
  Accesses++;
  if (Address==0x1000C238) return 1;
  if (Address<MUSB_BASE || Address>=MUSB_BASE+sizeof(Regs)) return 0;
  return Load32(Map(Address));
}
UINT8 EFIAPI MmioWrite8(UINTN Address, UINT8 V) {
  assert(!DeviceOwned);
  Accesses++;
  if (Address>=MUSB_BASE+0x20 && Address<MUSB_BASE+0x40) {
    UINTN Ep=(Address-MUSB_BASE-0x20)/4;
    assert(TxLen[Ep]<512); Tx[Ep][TxLen[Ep]++]=V;
  } else if (Address==MUSB_BASE+REG_INDEX) { assert(V<16); Selected=V; Regs[REG_INDEX]=V; }
  else if (Address==MUSB_BASE+REG_INTRUSB) Regs[REG_INTRUSB]&=~V;
  else *Map(Address)=V;
  return V;
}
UINT16 EFIAPI MmioWrite16(UINTN Address, UINT16 V) {
  Accesses++;
  if (Address==MUSB_BASE+REG_INTRTX || Address==MUSB_BASE+REG_INTRRX) {
    Store16(Map(Address), Load16(Map(Address))&~V); return V;
  }
  if (Address==MUSB_BASE+REG_TXCSR || Address==MUSB_BASE+REG_RXCSR) {
    BOOLEAN In=Address==MUSB_BASE+REG_RXCSR || (Selected==0 && (V&CSR0_REQPKT));
    BOOLEAN Start=Selected==0 ? (V&(CSR0_REQPKT|CSR0_TXREADY))!=0 :
                  In ? (V&RX_REQPKT)!=0 : (V&TX_READY)!=0;
    if (Start) {
      if (Active[Selected]<0) {
        assert(Next<ScriptCount);
        PACKET *P=&Script[Next];
        assert(P->Ep==Selected && P->In==In && P->Command==V);
        if (!In) {
          assert(TxLen[Selected]==P->Length);
          if (P->Length) assert(!memcmp(Tx[Selected],P->Bytes,P->Length));
          TxLen[Selected]=0;
        }
        Active[Selected]=Next++; NakDone[Selected]=FALSE;
      }
      Delay[Selected]=1;
    } else if (!(V&RX_READY) || Selected==0) {
      Active[Selected]=-1;
      if (Selected==0 || (V&(In ? RX_FLUSH:TX_FLUSH))) TxLen[Selected]=0;
    }
  }
  UINT16 New=V;
  if(Address==MUSB_BASE+REG_RXCSR) {
    New=(V&~RX_WZC_BITS)|(Load16(Map(Address))&V&RX_WZC_BITS);
  }
  Store16(Map(Address),New); return V;
}
UINT32 EFIAPI MmioWrite32(UINTN Address, UINT32 V) {
  assert(!DeviceOwned);
  Accesses++;
  if (Address<MUSB_BASE || Address>=MUSB_BASE+sizeof(Regs)) return V;
  if (Address==MUSB_BASE+REG_RXTOG || Address==MUSB_BASE+REG_TXTOG) {
    UINT32 Mask=V>>16, Old=Load32(Map(Address));
    Store32(Map(Address),(Old&~Mask)|(V&Mask));
  } else Store32(Map(Address),V);
  return V;
}
UINT32 EFIAPI MmioAnd32(UINTN A,UINT32 V) { return MmioWrite32(A,MmioRead32(A)&V); }
UINT32 EFIAPI MmioOr32(UINTN A,UINT32 V) { return MmioWrite32(A,MmioRead32(A)|V); }
UINT32 EFIAPI MmioAndThenOr32(UINTN A,UINT32 And,UINT32 Or) { return MmioWrite32(A,(MmioRead32(A)&And)|Or); }
static EFI_TPL EFIAPI Raise(EFI_TPL Tpl) { EFI_TPL Old=CurrentTpl; assert(Tpl>=Old); CurrentTpl=Tpl; return Old; }
static VOID EFIAPI Restore(EFI_TPL Tpl) { CurrentTpl=Tpl; }
static EFI_STATUS EFIAPI CreateEvent(UINT32 Type,EFI_TPL Tpl,EFI_EVENT_NOTIFY Notify,VOID *Context,EFI_EVENT *Event) {
  *Event=(EFI_EVENT)(UINTN)++EventCount; return 0;
}
static EFI_STATUS EFIAPI CreateEventEx(UINT32 Type,EFI_TPL Tpl,EFI_EVENT_NOTIFY Notify,CONST VOID *Context,CONST EFI_GUID *Group,EFI_EVENT *Event) {
  *Event=(EFI_EVENT)(UINTN)++EventCount; return 0;
}
static EFI_STATUS EFIAPI SetTimer(EFI_EVENT Event,EFI_TIMER_DELAY Type,UINT64 Trigger) { return 0; }
static EFI_STATUS EFIAPI CloseEvent(EFI_EVENT Event) { return 0; }
static EFI_STATUS EFIAPI Install(EFI_HANDLE *Handle,...) {
  va_list Args; va_start(Args,Handle); EFI_GUID *Guid;
  while((Guid=va_arg(Args,EFI_GUID*))!=NULL) {
    (void)va_arg(Args,VOID*);
    if(Guid==&gEfiUsb2HcProtocolGuid) HostPublished++;
    if(Guid==&gMediaTekUsbRoleProtocolGuid) RolePublished++;
  }
  va_end(Args); Published++; return 0;
}
static void Ready(void) {
  memset(&mHost,0,sizeof(mHost)); memset(Regs,0,sizeof(Regs)); memset(Eps,0,sizeof(Eps));
  memset(TxLen,0,sizeof(TxLen));
  for(int I=0;I<16;I++) Active[I]=-1;
  ScriptCount=Next=0; CurrentTpl=TPL_APPLICATION;
  mHost.State=EfiUsbHcStateOperational; mHost.Powered=mHost.WantPower=TRUE;
  mHost.MacReady=TRUE;
  mHost.HostAvailable=mHost.RoleAvailable=TRUE;
  mHost.ActiveRole=mHost.DesiredRole=MtkUsbRoleHost;
  Sink=TRUE; PcConnected=DeviceOwned=FALSE; ConnectorEvents=0;
  RoleReadStatus=EFI_SUCCESS;
  Store16(&Regs[REG_HWVERS],0xE503);
  mHost.Port.PortStatus=USB_PORT_STAT_POWER|USB_PORT_STAT_CONNECTION|USB_PORT_STAT_ENABLE;
  mHost.RoleDue=TimeNs+1000000000ULL;
}
static void Add(UINT8 Ep, BOOLEAN In, UINT16 Cmd, UINTN N, const void *Data) {
  assert(ScriptCount<32);
  Script[ScriptCount++]=(PACKET){.Ep=Ep,.In=In,.Command=Cmd,.Length=N,.Bytes=Data};
}
static void Tick(void) { CurrentTpl=TPL_CALLBACK; HostTick(NULL,NULL); CurrentTpl=TPL_APPLICATION; }
static UINTN Callbacks;
static EFI_STATUS EFIAPI Callback(VOID *Data, UINTN N, VOID *Context, UINT32 Result) {
  assert(CurrentTpl==TPL_CALLBACK && !Result && N==8 && ((UINT8*)Data)[2]==4);
  UINT8 Toggle=0;
  assert(HostAsync(&mHost.Protocol,3,0x81,EFI_USB_SPEED_FULL,8,FALSE,&Toggle,0,0,NULL,NULL,NULL)==0);
  assert(Toggle==1); Callbacks++; return EFI_SUCCESS;
}
int main(void) {
  Services.RaiseTPL=Raise; Services.RestoreTPL=Restore;
  Services.CreateEvent=CreateEvent; Services.CreateEventEx=CreateEventEx;
  Services.SetTimer=SetTimer; Services.CloseEvent=CloseEvent;
  Services.InstallMultipleProtocolInterfaces=Install;
  EFI_USB2_HC_PROTOCOL *H=&mHost.Protocol;
  UINT32 Result; UINT8 Toggle; UINTN N; UINT8 Buffer[1024], Data[512];
  for(UINTN I=0;I<sizeof(Data);I++) Data[I]=(UINT8)I;
  Ready();
  // EP0 GET_DESCRIPTOR with multiple packets, short IN and OUT status ZLP.
  EFI_USB_DEVICE_REQUEST Req={0x80,6,0x100,0,18};
  Add(0,FALSE,CSR0_SETUP|CSR0_TXREADY,8,&Req);
  Add(0,TRUE,CSR0_REQPKT,8,Data); Add(0,TRUE,CSR0_REQPKT,8,Data+8);
  Add(0,TRUE,CSR0_REQPKT,2,Data+16); Add(0,FALSE,CSR0_STATUS|CSR0_TXREADY,0,NULL);
  N=sizeof(Buffer);
  assert(HostControl(H,0,EFI_USB_SPEED_FULL,8,&Req,EfiUsbDataIn,Buffer,&N,100,NULL,&Result)==0);
  assert(N==18 && !memcmp(Buffer,Data,18) && Next==5 && CurrentTpl==TPL_APPLICATION);
  Ready();
  // No-data SET_ADDRESS uses an IN status, no premature remote address change.
  Req=(EFI_USB_DEVICE_REQUEST){0,5,7,0,0};
  Add(0,FALSE,CSR0_SETUP|CSR0_TXREADY,8,&Req); Add(0,TRUE,CSR0_STATUS|CSR0_REQPKT,0,NULL);
  assert(HostControl(H,0,EFI_USB_SPEED_FULL,8,&Req,EfiUsbNoData,NULL,NULL,10,NULL,&Result)==0);
  assert(Load16(&Regs[0x480])==0);
  Ready();
  // OUT control data, then status IN. NAK retry must retain the original FIFO.
  Req=(EFI_USB_DEVICE_REQUEST){0x21,9,0x200,0,1};
  Add(0,FALSE,CSR0_SETUP|CSR0_TXREADY,8,&Req);
  Add(0,FALSE,CSR0_TXREADY,1,Data); Script[1].RetryNak=TRUE;
  Add(0,TRUE,CSR0_STATUS|CSR0_REQPKT,0,NULL);
  N=1; assert(HostControl(H,3,EFI_USB_SPEED_FULL,8,&Req,EfiUsbDataOut,Data,&N,10,NULL,&Result)==0);
  assert(N==1 && Next==3);
  Ready();
  // Mass-storage EP2 OUT (not hard-coded endpoint 1), multi-packet/toggle.
  Add(1,FALSE,TX_MODE|TX_READY,512,Data); Add(1,FALSE,TX_MODE|TX_READY,31,Data);
  memcpy(Buffer,Data,512); memcpy(Buffer+512,Data,31);
  VOID *Buffers[10]={Buffer}; N=543; Toggle=0;
  assert(HostBulk(H,7,2,EFI_USB_SPEED_HIGH,512,1,Buffers,&N,&Toggle,100,NULL,&Result)==0);
  assert(N==543 && Toggle==0 && Eps[1][10]==0x62 && Load16(&Regs[0x488])==7);
  Ready();
  Add(1,TRUE,RX_REQPKT,64,Data); Add(1,TRUE,RX_REQPKT,7,Data+64);
  N=128; Toggle=0;
  assert(HostBulk(H,4,0x83,EFI_USB_SPEED_FULL,64,1,Buffers,&N,&Toggle,10,NULL,&Result)==0);
  assert(N==71 && !memcmp(Buffer,Data,71) && Toggle==0 && Eps[1][12]==0xA3 && Load16(&Regs[0x48C])==4);
  Ready();
  Add(1,TRUE,RX_REQPKT,64,Data); Add(1,TRUE,RX_REQPKT,64,Data); Script[1].Timeout=TRUE;
  N=128; Toggle=0;
  assert(HostBulk(H,1,0x81,0,64,1,Buffers,&N,&Toggle,1,NULL,&Result)==EFI_TIMEOUT);
  assert(Result==EFI_USB_ERR_TIMEOUT && N==64 && Toggle==1 && Active[1]<0);
  Ready();
  Add(1,TRUE,RX_REQPKT,0,NULL); Script[0].Failure=RX_STALL;
  N=8; Toggle=0;
  assert(HostInterrupt(H,1,0x81,0,8,Buffer,&N,&Toggle,10,NULL,&Result)==EFI_DEVICE_ERROR);
  assert(Result==EFI_USB_ERR_STALL && N==0 && Toggle==0);
  Ready();
  Add(1,TRUE,RX_REQPKT,9,Data); memset(Buffer,0xCC,16); N=8; Toggle=0;
  assert(HostInterrupt(H,1,0x81,0,16,Buffer,&N,&Toggle,10,NULL,&Result)==EFI_DEVICE_ERROR);
  assert(Result==EFI_USB_ERR_BABBLE && Buffer[8]==0xCC && N==0);
  // Reject unsupported TT routes without programming target registers.
  EFI_USB2_HC_TRANSACTION_TRANSLATOR TT={2,1}; UINTN Old=Accesses; N=8;
  assert(HostInterrupt(H,1,0x81,0,8,Buffer,&N,&Toggle,10,&TT,&Result)==EFI_UNSUPPORTED);
  assert(Accesses==Old);
  Ready();
  UINT8 Key[8]={0,0,4}; Add(2,TRUE,RX_REQPKT,8,Key); Toggle=0;
  assert(HostAsync(H,3,0x81,0,8,TRUE,&Toggle,10,8,NULL,Callback,NULL)==0);
  Tick(); assert(Callbacks==0); Tick(); Tick();
  assert(Callbacks==1 && !mHost.Async[0].Used && Active[2]<0);
  Ready(); Callbacks=0;
  Add(2,TRUE,RX_REQPKT,0,NULL); Script[0].NoReply=TRUE;
  Add(2,TRUE,RX_REQPKT|RX_WZC_BITS,8,Key); Toggle=0;
  assert(HostAsync(H,3,0x81,0,8,TRUE,&Toggle,10,8,NULL,Callback,NULL)==0);
  Tick(); Tick(); Tick(); assert(!Callbacks && Next==1);
  TimeNs+=10000000; Tick(); Tick(); Tick();
  assert(Callbacks==1 && Next==2); // idle hardware request re-armed, no invented report
  Ready();
  Add(2,TRUE,RX_REQPKT,8,Key); Script[0].Timeout=TRUE; Toggle=0;
  assert(HostAsync(H,3,0x81,0,8,TRUE,&Toggle,10,8,NULL,Callback,NULL)==0);
  Tick(); assert(mHost.Async[0].Pending);
  assert(HostClearPort(H,0,EfiUsbPortEnable)==0);
  assert(Active[2]<0 && !mHost.Async[0].Pending && mHost.Async[0].Failed);
  assert(mHost.Port.PortChangeStatus&USB_PORT_STAT_C_ENABLE);
  assert(HostAsync(H,3,0x81,0,8,FALSE,&Toggle,0,0,NULL,NULL,NULL)==0);
  Ready();
  // Root reset minimum duration, HS status and W1C software change bits.
  assert(HostSetPort(H,0,EfiUsbPortReset)==0);
  UINT64 Start=TimeNs; Regs[REG_POWER]|=POWER_HSMODE;
  assert(HostClearPort(H,0,EfiUsbPortReset)==0 && TimeNs-Start>=70000000);
  assert(mHost.Port.PortStatus&USB_PORT_STAT_HIGH_SPEED);
  assert(mHost.Port.PortChangeStatus&USB_PORT_STAT_C_RESET);
  assert(HostClearPort(H,0,EfiUsbPortResetChange)==0 && !(mHost.Port.PortChangeStatus&USB_PORT_STAT_C_RESET));
  TimeNs+=20000000;
  Regs[REG_INTRUSB]=USB_DISCONNECT; EFI_USB_PORT_STATUS Port;
  assert(HostPortStatus(H,0,&Port)==0 && !(Port.PortStatus&USB_PORT_STAT_CONNECTION));
  assert(Port.PortChangeStatus&USB_PORT_STAT_C_CONNECTION);
  // Coalesced unplug/replug: report the new connection and retire old pipes.
  Ready(); mHost.Configured=TRUE;
  mHost.Async[0].Used=TRUE;
  Regs[REG_DEVCTL]=DEVCTL_HOST_MODE|DEVCTL_SESSION|DEVCTL_LSDEV;
  Regs[REG_INTRUSB]=USB_DISCONNECT|USB_CONNECT;
  assert(HostPortStatus(H,0,&Port)==0 && (Port.PortStatus&USB_PORT_STAT_CONNECTION));
  assert((Port.PortStatus&USB_PORT_STAT_LOW_SPEED) && !(Port.PortStatus&USB_PORT_STAT_ENABLE));
  assert(!mHost.Configured && mHost.Async[0].Failed && (Port.PortChangeStatus&USB_PORT_STAT_C_CONNECTION));
  // Missing connect interrupt, then missing disconnect interrupt with adapter retained.
  Ready(); mHost.Port.PortStatus=USB_PORT_STAT_POWER;
  Regs[REG_DEVCTL]=DEVCTL_HOST_MODE|DEVCTL_SESSION|DEVCTL_FSDEV; Regs[REG_OPSTATE]=0x22;
  BusEvents(); assert(!(mHost.Port.PortStatus&USB_PORT_STAT_CONNECTION));
  TimeNs+=20000000; BusEvents(); assert(mHost.Port.PortStatus&USB_PORT_STAT_CONNECTION);
  Regs[REG_OPSTATE]=OPSTATE_HOST_WAIT_DEVICE; BusEvents();
  TimeNs+=20000000; BusEvents(); assert(!(mHost.Port.PortStatus&USB_PORT_STAT_CONNECTION));
  assert(mHost.DeviceRemoved && mHost.Powered);
  // No false HS disconnect based only on LSDEV/FSDEV being zero.
  Ready(); mHost.Port.PortStatus|=USB_PORT_STAT_HIGH_SPEED;
  Regs[REG_DEVCTL]=DEVCTL_HOST_MODE|DEVCTL_SESSION; Regs[REG_OPSTATE]=0x22;
  BusEvents(); TimeNs+=40000000; BusEvents(); assert(mHost.Port.PortStatus&USB_PORT_STAT_CONNECTION);
  // Mask reset-related chatter, but not a later real removal.
  Ready(); HostSetPort(H,0,EfiUsbPortReset); Regs[REG_INTRUSB]=USB_DISCONNECT;
  Regs[REG_OPSTATE]=OPSTATE_HOST_WAIT_DEVICE; BusEvents(); TimeNs+=30000000; BusEvents();
  assert(mHost.Port.PortStatus&USB_PORT_STAT_CONNECTION);
  // Re-arm an ended MAC session while retaining Type-C/VBUS ownership.
  Ready(); mHost.Partner=TRUE; mHost.Port.PortStatus=USB_PORT_STAT_POWER;
  Regs[REG_DEVCTL]=0; RecoverSession(); assert(Regs[REG_DEVCTL]&DEVCTL_SESSION);
  assert(mHost.SessionRestarts==1); RecoverSession(); assert(mHost.SessionRestarts==1);
  Ready();
  PowerOff(); mHost.RoleDue=0; Sink=FALSE; Old=PowerEnables; Tick();
  assert(!Vbus && PowerEnables==Old); Sink=TRUE; TimeNs+=50000000; Tick();
  assert(!Vbus); TimeNs+=50000000; Tick(); assert(Vbus && PowerEnables==Old+1);
  Regs[REG_INTRUSB]=USB_VBUS_ERROR; Tick(); assert(!Vbus && mHost.Fault);
  Vbus=TRUE; FailPowerOff=TRUE; PowerOff(); assert(mHost.PowerOffPending && Vbus);
  FailPowerOff=FALSE; Sink=FALSE; CheckRole(); assert(!mHost.PowerOffPending && !Vbus);
  HostExit(NULL,NULL); Old=Accesses; Tick(); assert(Accesses==Old);
  // Screen text follows completed descriptors/configuration and bounds-checks
  // descriptor walking. Rendering consumes cached state, never controller MMIO.
  Ready(); mHost.Partner=TRUE;
  UINT8 Desc[18]={18,1,0,2,0,0,0,64,0xEF,0x17,0xAC,0x38};
  Req=(EFI_USB_DEVICE_REQUEST){0x80,6,0x100,0,18};
  ObserveControl(3,&Req,Desc,sizeof(Desc));
  assert(mHost.VendorId==0x17EF && mHost.ProductId==0x38AC && !mHost.Configured);
  UINT8 Config[]={9,2,18,0,1,1,0,0x80,50, 9,4,0,0,1,8,6,0x50,0};
  Req.Value=0x200; ObserveControl(3,&Req,Config,sizeof(Config));
  assert(mHost.DeviceKind==2 && !mHost.Configured);
  Req=(EFI_USB_DEVICE_REQUEST){0,9,1,0,0}; ObserveControl(3,&Req,NULL,0);
  CHAR8 State[44], Detail[44]; DescribeStatus(State,sizeof(State),Detail,sizeof(Detail));
  assert(strstr(State,"STORAGE CONFIGURED") && strstr(Detail,"17ef:38ac"));
  Config[9]=0; Req=(EFI_USB_DEVICE_REQUEST){0x80,6,0x200,0,sizeof(Config)};
  ObserveControl(3,&Req,Config,sizeof(Config)); assert(mHost.DeviceKind==2);
  Req=(EFI_USB_DEVICE_REQUEST){0,5,4,0,0}; ObserveControl(0,&Req,NULL,0);
  assert(mHost.DeviceAddress==4 && !mHost.Configured && !mHost.DeviceKind && !mHost.VendorId);
  mHost.DeviceKind=1; mHost.DeviceAddress=3;
  UINT8 Idle[8]={0}, Caps[8]={0,0,0x39};
  ObserveKeyboard(3,Idle,8); assert(mHost.HidReports==1 && !mHost.KeyReports);
  ObserveKeyboard(3,Caps,8); assert(mHost.HidReports==2 && mHost.LastKey==0x39);
  UINT8 Led=2;
  Req=(EFI_USB_DEVICE_REQUEST){0x21,9,0x200,0,1};
  Add(0,FALSE,CSR0_SETUP|CSR0_TXREADY,8,&Req); Add(0,FALSE,CSR0_TXREADY,1,&Led);
  Add(0,TRUE,CSR0_STATUS|CSR0_REQPKT,0,NULL); N=1;
  assert(HostControl(H,3,EFI_USB_SPEED_LOW,8,&Req,EfiUsbDataOut,&Led,&N,10,NULL,&Result)==0);
  assert(mHost.LedRequests==1 && mHost.LedCompleted==1 && mHost.LedValue==2);
  mHost.LastError=EFI_TIMEOUT; mHost.ErrorPhase="I2C / Type-C";
  DescribeStatus(State,sizeof(State),Detail,sizeof(Detail)); assert(strstr(State,"ERROR"));
  Old=Accesses; HostScreen(NULL,NULL); assert(ScreenWrites==2 && Accesses==Old);
  mHost.Stopped=TRUE; HostScreen(NULL,NULL); assert(ScreenWrites==2);
  memset(&mHost,0,sizeof(mHost)); BoardInitResult=EFI_TIMEOUT;
  Old=Accesses;
  assert(MusbHostEntry(NULL,NULL)==EFI_SUCCESS); // status-only residency
  assert(HostPublished==0 && RolePublished==1 && mHost.LastError==EFI_TIMEOUT && !mHost.MacReady);
  assert(mHost.ScreenEvent && mHost.ExitEvent && mHost.PollEvent);
  HostScreen(NULL,NULL); HostExit(NULL,NULL);
  assert(Accesses==Old); // failed board init must not touch an unclocked MAC
  // Host -> PC Device -> Host -> Device. Old Host callers must never read,
  // acknowledge or write device EP registers while fastboot owns the MAC.
  Ready(); BoardInitResult=0; mHost.Partner=TRUE;
  assert(RegisterDevice(&mUsbRole,DeviceOn,DeviceOff,NULL)==0);
  Sink=FALSE; PcConnected=TRUE; CheckRole(); TimeNs+=50000000; CheckRole();
  assert(DeviceOwned && !Vbus && mHost.ActiveRole==MtkUsbRoleDevice);
  RoleReadStatus=EFI_TIMEOUT; CheckRole(); assert(DeviceOwned);
  RoleReadStatus=EFI_SUCCESS;
  Old=Accesses; HostPortStatus(H,0,&Port); HostClearPort(H,0,EfiUsbPortEnable);
  HostReset(H,EFI_USB_HC_RESET_GLOBAL); HostSetState(H,EfiUsbHcStateOperational);
  N=8; Toggle=0;
  assert(HostInterrupt(H,1,0x81,0,8,Buffer,&N,&Toggle,1,NULL,&Result)==EFI_DEVICE_ERROR);
  Tick(); assert(Accesses==Old && DeviceOwned);
  Sink=TRUE; PcConnected=FALSE; CheckRole(); assert(DeviceOwned);
  TimeNs+=50000000; CheckRole(); assert(!DeviceOwned && !Vbus);
  TimeNs+=50000000; CheckRole(); assert(mHost.ActiveRole==MtkUsbRoleHost && Vbus);
  Sink=FALSE; PcConnected=TRUE; CheckRole(); TimeNs+=50000000; CheckRole();
  assert(DeviceOwned && DeviceStarts==2 && DeviceStops==1);
  HostExit(NULL,NULL); assert(!DeviceOwned && DeviceStops==2);
  // Board-init failure still permits the original PC-facing device path.
  Ready(); mHost.RoleAvailable=mHost.HostAvailable=mHost.MacReady=FALSE;
  mHost.ActiveRole=mHost.DesiredRole=MtkUsbRoleNone; mHost.Powered=FALSE; Vbus=FALSE;
  assert(RegisterDevice(&mUsbRole,DeviceOn,DeviceOff,NULL)==0);
  Old=Accesses; CheckRole(); TimeNs+=50000000; CheckRole();
  assert(DeviceOwned && Accesses==Old); HostExit(NULL,NULL);
  // Persistent historical DETACH hints must not reset activation's timer or
  // tear down a stable, authoritative HOST status on every read.
  Ready(); PowerOff(); mHost.ActiveRole=mHost.DesiredRole=MtkUsbRoleNone;
  mHost.Partner=FALSE;
  for(int I=0;I<8;I++) { ConnectorEvents=BIT1; CheckRole(); TimeNs+=50000000; }
  assert(mHost.ActiveRole==MtkUsbRoleHost && Vbus && mHost.RoleSwitches==1);
  Old=PowerEnables;
  // Brief unconnected sample is debounced, without cycling working Host power.
  Sink=FALSE; CheckRole(); TimeNs+=50000000;
  Sink=TRUE; CheckRole(); TimeNs+=50000000; CheckRole();
  assert(mHost.ActiveRole==MtkUsbRoleHost && Vbus && PowerEnables==Old);
  // A real unplug settles, stops Host, then the PC/source selects Device.
  assert(RegisterDevice(&mUsbRole,DeviceOn,DeviceOff,NULL)==0);
  Sink=FALSE; CheckRole(); TimeNs+=150000000; CheckRole();
  assert(mHost.ActiveRole==MtkUsbRoleNone && !Vbus);
  TimeNs+=50000000; CheckRole();
  PcConnected=TRUE; CheckRole(); TimeNs+=50000000; CheckRole();
  assert(DeviceOwned && mHost.TypeCStatus==0x88 && mHost.DeviceStartAttempts==1);
  HostExit(NULL,NULL);
  // Idle state always reports its sampling/registration heartbeat, so a frozen
  // no-OS screen cannot be mistaken for a continuing CC search.
  Ready(); mHost.ActiveRole=mHost.DesiredRole=MtkUsbRoleNone;
  mHost.Partner=mHost.Powered=FALSE; mHost.TypeCStatus=0; mHost.PollCount=123;
  DescribeStatus(State,sizeof(State),Detail,sizeof(Detail));
  assert(strstr(State,"WAITING FOR OTG") && strstr(Detail,"T=123") && strstr(Detail,"D=0"));
  puts("PASS: MUSB host control/bulk, NAK/STALL/timeout, short packets, toggle, async self-cancel, root port, role gating, EBS (ASan/UBSan)");
  puts("PASS: cached Host status rendering and bounded descriptor classification");
  puts("PASS: lost/coalesced port events, session/IN rearm, HID/LED observations and exclusive dual-role handoff");
  puts("PASS: stable-role debounce, repeated DETACH hints, real unplug-to-PC handoff and idle heartbeat");
}
