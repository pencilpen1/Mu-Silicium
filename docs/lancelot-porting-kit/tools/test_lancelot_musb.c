/** Host tests of the actual MUSB EP0/bulk state machine with mocked MMIO. */
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
static BOOLEAN Irq=TRUE;
BOOLEAN EFIAPI ArmGetInterruptState(VOID) { return Irq; }
UINTN EFIAPI ArmDisableInterrupts(VOID) { UINTN Old=Irq; Irq=FALSE; return Old; }
VOID EFIAPI ArmEnableInterrupts(VOID) { Irq=TRUE; }
VOID EFIAPI MemoryFence(VOID) {}
VOID EFIAPI BootLogPrint(CONST CHAR8 *Format, ...) {} // telemetry formatting only
#define MT6768_BOOT_SCREEN_H
static void BootScreenHex(UINTN Y, CONST CHAR8 *Name, UINT64 V, UINT32 C) {}
#include "ws/MT6768Pkg/Drivers/MusbDiagnosticDxe/MusbDiagnostic.c"

EFI_BOOT_SERVICES *gBS;
EFI_GUID gEfiEventExitBootServicesGuid, gEfiEventReadyToBootGuid;
static UINT8 Reg[4096], Ep[16][16], Selected;
static UINT8 Input[2][128], Output[2][128];
static UINTN InputPos[2], OutputLength[2];
static UINT16 LastCsr[16];
static UINTN AckReleaseDelay;
static BOOLEAN AckPending, AckStuck, AckNeedsReadback;

static UINT8 *Addr(UINTN A) {
  assert(A >= MAC && A < MAC + sizeof(Reg));
  A -= MAC;
  if(A >= 0x10 && A < 0x20) return &Ep[Selected][A-0x10];
  return &Reg[A];
}
UINT8 EFIAPI MmioRead8(UINTN A) {
  if(A == MAC+FIFO0 || A == MAC+FIFO1) {
    UINTN E=A == MAC+FIFO0 ? 0:1;
    assert(InputPos[E] < sizeof(Input[E])); return Input[E][InputPos[E]++];
  }
  return *Addr(A);
}
UINT16 EFIAPI MmioRead16(UINTN A) {
  UINT16 V;
  memcpy(&V,Addr(A),2);
  if(A==MAC+TXCSR && Selected==0 && AckPending) {
    if(!AckStuck && AckReleaseDelay==0) {
      V &= ~EP0_RX;
      memcpy(Addr(A),&V,2);
      AckPending=FALSE;
    } else if(AckReleaseDelay!=0) {
      AckReleaseDelay--;
    }
  }
  if(A==MAC+TXCSR && Selected==0 && !(V&EP0_RX)) AckNeedsReadback=FALSE;
  return V;
}
UINT32 EFIAPI MmioRead32(UINTN A) { UINT32 V; memcpy(&V,Addr(A),4);return V; }
UINT8 EFIAPI MmioWrite8(UINTN A,UINT8 V) {
  if(A == MAC+FIFO0 || A == MAC+FIFO1) {
    UINTN E=A == MAC+FIFO0 ? 0:1;
    if(E==0) {
      UINT16 C; memcpy(&C,&Ep[0][2],2);
      // The shared FIFO cannot accept a TX packet while SETUP owns it.
      // Require the driver's explicit RXPKTRDY-clear readback as well.
      assert(!(C&EP0_RX) && !AckNeedsReadback);
    }
    assert(OutputLength[E] < sizeof(Output[E])); Output[E][OutputLength[E]++]=V;
  } else if(A == MAC+INDEX) { assert(V<16); Selected=V;Reg[INDEX]=V; }
  else if(A == MAC+INTRUSB) { Reg[INTRUSB]&=~V; }
  else *Addr(A)=V;
  return V;
}
UINT16 EFIAPI MmioWrite16(UINTN A,UINT16 V) {
  UINT16 New=V, Old; memcpy(&Old,Addr(A),2);
  if(A == MAC+TXCSR) {
    LastCsr[Selected]=V;
    if(Selected==0) {
      New=Old;
      if(V&EP0_ACK_RX) {
        if(!(V&(EP0_END|EP0_STALL))) {
          assert(!(V&EP0_TX));
          AckPending=TRUE;
          AckNeedsReadback=TRUE;
        } else New&=~EP0_RX;
      }
      if(V&EP0_ACK_END) New&=~EP0_SETUPEND;
      if(V&EP0_TX) New|=EP0_TX;
      if(V&EP0_END) New|=EP0_END;
      if(V&EP0_STALL) New|=EP0_STALL;
      if(V==0 || V==0x100) New=0;
    }
  }
  if(A == MAC+INTRTX || A == MAC+INTRRX) New=Old&~V;
  memcpy(Addr(A),&New,2);return V;
}
UINT32 EFIAPI MmioWrite32(UINTN A,UINT32 V) { memcpy(Addr(A),&V,4);return V; }
UINT32 EFIAPI MmioAnd32(UINTN A,UINT32 V) {return MmioWrite32(A,MmioRead32(A)&V);}
UINT32 EFIAPI MmioOr32(UINTN A,UINT32 V) {return MmioWrite32(A,MmioRead32(A)|V);}
UINT32 EFIAPI MmioAndThenOr32(UINTN A,UINT32 And,UINT32 Or) {return MmioWrite32(A,(MmioRead32(A)&And)|Or);}
UINTN EFIAPI MicroSecondDelay(UINTN N) {return N;}
VOID *EFIAPI CopyMem(VOID *D,CONST VOID *S,UINTN N) {return memmove(D,S,N);}
UINTN EFIAPI AsciiStrLen(CONST CHAR8 *S) {return strlen(S);}
INTN EFIAPI AsciiStrCmp(CONST CHAR8 *A,CONST CHAR8 *B) {return strcmp(A,B);}

static void Csr(UINTN E,UINT16 V) {
  memcpy(&Ep[E][2],&V,2);
  if(E==0) AckPending=AckNeedsReadback=FALSE;
}
static void Count(UINTN E,UINT16 V) {memcpy(&Ep[E][8],&V,2);}
static void SubmitRequest(UINT8 T,UINT8 R,UINT16 V,UINT16 I,UINT16 L) {
  UINT8 S[]={T,R,V,V>>8,I,I>>8,L,L>>8};
  memcpy(Input[0],S,8);InputPos[0]=0;OutputLength[0]=0;
  Csr(0,EP0_RX);Count(0,8);
  PollControl(TRUE);
}
static void Request(UINT8 T,UINT8 R,UINT16 V,UINT16 I,UINT16 L) {
  SubmitRequest(T,R,V,I,L);
  if(mControlState==ControlSetupAck) {
    assert(OutputLength[0]==0 && LastCsr[0]==EP0_ACK_RX);
    PollControl(FALSE); // FIFO handover does not require another IRQ
  }
}
static void StatusOut(void) {
  // Controller consumes the status ZLP and drops DATAEND itself.
  Csr(0,0);PollControl(TRUE);assert(mControlState==ControlIdle);
}
static void Bulk(CONST CHAR8 *Text) {
  UINTN N=strlen(Text);assert(N<=64);
  memcpy(Input[1],Text,N);InputPos[1]=OutputLength[1]=0;
  Csr(1,0x2000);memcpy(&Ep[1][6],(UINT16[]){1},2);Count(1,N);
  PollBulk();
}
int main(int argc, char **argv) {
  BusReset();assert(mConfiguration==0 && mPendingAddress==-1 && Reg[FADDR]==0);
  Request(0x80,6,0x100,0,8);
  assert(OutputLength[0]==8 && !memcmp(Output[0],mDevice,8));
  Csr(0,EP0_END);PollControl(TRUE);
  assert(mControlState==ControlStatusOut && mControlCompletions==0);
  StatusOut();
  Request(0x80,6,0x100,0,255);
  assert(OutputLength[0]==18 && !memcmp(Output[0],mDevice,18));StatusOut();
  Request(0x80,6,0x200,0,255);
  assert(OutputLength[0]==32 && Output[0][20]==0x81 && Output[0][29]==64);StatusOut();
  Request(0x80,6,0x300,0,255);assert(OutputLength[0]==4);StatusOut();
  Request(0x80,6,0x303,0x409,255);assert(OutputLength[0]==28);StatusOut();
  Request(0x80,6,0x600,0,10);assert(LastCsr[0]&EP0_STALL);
  Request(0,5,17,0,0);
  assert(mPendingAddress==17 && Reg[FADDR]==0);
  // Stale/coalesced IRQ while DATAEND is high must not apply the address.
  PollControl(TRUE);assert(mPendingAddress==17 && Reg[FADDR]==0);
  Csr(0,0);PollControl(FALSE);assert(Reg[FADDR]==0);
  PollControl(TRUE);assert(Reg[FADDR]==17 && mAddress==17 && mPendingAddress==-1);
  Request(0,9,1,0,0);assert(mConfiguration==1 && mStage==6);
  Csr(0,0);PollControl(TRUE);
  Request(0x80,8,0,0,1);assert(Output[0][0]==1);StatusOut();
  Request(0x81,10,0,0,1);assert(Output[0][0]==0);StatusOut();
  Request(0x82,0,0,0x81,2);assert(Output[0][0]==0);StatusOut();
  Request(2,3,0,0x81,0);assert(mHaltIn);
  Request(0x82,0,0,0x81,2);assert(Output[0][0]==1);StatusOut();
  Request(2,1,0,0x81,0);assert(!mHaltIn);
  Bulk("getvar:product");
  assert(OutputLength[1]==17 && !memcmp(Output[1],"OKAYlancelot-uefi",17));
  Bulk("flash:boot");assert(!memcmp(Output[1],"FAIL",4));
  Bulk("download:00000010");assert(!memcmp(Output[1],"FAIL",4));
  assert(mCommands==3);
  // Exercise multi-packet EP0 and the exact-multiple short-transfer ZLP.
  UINT8 Large[64];memset(Large,0x55,sizeof(Large));
  W8(INDEX,0);OutputLength[0]=0;Csr(0,EP0_RX);
  ControlReply(Large,sizeof(Large),100);
  assert(mControlState==ControlSetupAck && OutputLength[0]==0);
  PollControl(FALSE);
  assert(mControlState==1 && !(LastCsr[0]&EP0_END));
  Csr(0,0);PollControl(TRUE);
  assert(mControlState==3 && (LastCsr[0]&EP0_END) && OutputLength[0]==64);StatusOut();
  Request(0,9,0,0,0);assert(!mConfiguration);
  Bulk("getvar:product");assert(OutputLength[1]==0);
  // Invalid requests stall, rather than acknowledging nonexistent endpoints.
  Request(0,5,128,0,0);assert(LastCsr[0]&EP0_STALL);
  Request(0x82,0,0,0x85,2);assert(LastCsr[0]&EP0_STALL);
  BusReset();assert(mAddress==0 && mControlState==0);
  // Delayed shared-FIFO release: no retransmission/reparse while ACK pending.
  AckReleaseDelay=2;
  SubmitRequest(0x80,6,0x100,0,64);
  UINT32 Setups=mSetups;
  PollControl(FALSE);PollControl(TRUE);
  assert(OutputLength[0]==0 && mControlState==ControlSetupAck && mSetups==Setups);
  PollControl(FALSE);
  assert(OutputLength[0]==18 && mControlState==ControlStatusOut);
  // STATUSOUT completion and the next setup in the very same interrupt.
  Request(0x80,6,0x200,0,32);
  assert(OutputLength[0]==32 && mControlState==ControlStatusOut);
  StatusOut();
  // Bad hardware handover is bounded and records an error, not a busy loop.
  AckStuck=TRUE;
  SubmitRequest(0x80,6,0x100,0,64);
  for(UINTN I=0;I<100;I++) PollControl(FALSE);
  assert(mSetupAckTimeouts==1 && mControlState==ControlIdle && OutputLength[0]==0);
  AckStuck=FALSE;Csr(0,0);
  BusReset();
  Request(0,5,31,0,0);
  Csr(0,EP0_SETUPEND);PollControl(TRUE);
  assert(mPendingAddress==-1 && Reg[FADDR]==0);
  // Bus RESET wins over a pending address/status interrupt in the same poll.
  mActive=TRUE; mPendingAddress=44; mControlState=2;
  Reg[INTRUSB]=RESET_IRQ;Reg[INTRTX]=1;
  Poll(NULL,NULL);
  assert(mAddress==0 && mPendingAddress==-1 && !Reg[INTRUSB] && !Reg[INTRTX]);

  // End-to-end RAM snapshot -> real bulk state machine -> INFO framing.
  UINT8 Original[6000], Received[4096];
  for(UINTN I=0;I<sizeof(Original);I++) Original[I]=(UINT8)I;
  BootLogInitialize(0x1122334455667788ULL);
  BootLogWrite(Original,sizeof(Original));
  mConfiguration=1; ResetBulk();
  Bulk("oem log 0000000000000000");
  assert(OutputLength[1]==60 && !memcmp(Output[1],
    "INFOLLOG1 1122334455667788 0000000000000000 0000000000001000",60));
  FILE *Transcript=argc>1 ? fopen(argv[1],"wb") : NULL;
  if(argc>1) assert(Transcript);
  if(Transcript) fprintf(Transcript,"(bootloader) %.*s\n",(int)OutputLength[1]-4,Output[1]+4);
  PollBulk(); // a slow/disconnected PC leaves TX busy: never overwrite FIFO
  assert(OutputLength[1]==60 && mLogOffset==0);
  // Ring wraps while USB is draining: the in-flight snapshot must be stable.
  UINT8 *Overwrite=malloc(LOG_CAPACITY); assert(Overwrite);
  memset(Overwrite,0xee,LOG_CAPACITY); BootLogWrite(Overwrite,LOG_CAPACITY); free(Overwrite);
  UINTN ReceivedCount=0, Packets=0;
  while(mLogReply!=LogReplyNone) {
    Csr(1,0x2000); OutputLength[1]=0; PollBulk();
    UINTN Len=OutputLength[1];
    assert(Len>0 && Len<64 && ++Packets<180);
    if(Len==4) { assert(!memcmp(Output[1],"OKAY",4)); break; }
    assert(!memcmp(Output[1],"INFOLLOGD ",10) && (Len-10)%2==0);
    if(Transcript) fprintf(Transcript,"(bootloader) %.*s\n",(int)Len-4,Output[1]+4);
    for(UINTN I=10;I<Len;I+=2) {
      unsigned Value; char Hex[3]={Output[1][I],Output[1][I+1],0};
      assert(sscanf(Hex,"%x",&Value)==1 && ReceivedCount<sizeof(Received));
      Received[ReceivedCount++]=(UINT8)Value;
    }
  }
  assert(ReceivedCount==4096 && !memcmp(Received,Original,4096) && Irq);
  if(Transcript) { fputs("OKAY [ 0.200s]\nFinished. Total time: 0.201s\n",Transcript); fclose(Transcript); }
  Bulk("getvar:product"); assert(!memcmp(Output[1],"OKAYlancelot-uefi",17));
  Bulk("oem log z000000000000000"); assert(!memcmp(Output[1],"FAIL",4));
  Bulk("oem log 000000000000000"); assert(!memcmp(Output[1],"FAIL",4));
  Bulk("oem log 0000000000000000"); assert(mLogReply==LogReplyData);
  BusReset(); assert(mLogReply==LogReplyNone && !mConfiguration);
  assert(LOG->Written==sizeof(Original)+LOG_CAPACITY); // reset preserves logs
  BootLogInitialize(100);
  mConfiguration=1; ResetBulk();
  Bulk("oem log 0000000000000000"); assert(mLogLength==0 && OutputLength[1]==60);
  Csr(1,0x2000); OutputLength[1]=0; PollBulk();
  assert(OutputLength[1]==4 && !memcmp(Output[1],"OKAY",4) && mLogReply==LogReplyNone);
  // ExitBootServices detaches and later timer notifications do no MMIO.
  W8(POWER,SOFTCONN);Stop(NULL,NULL);
  assert(mStopped && !mActive && Reg[POWER]==0);
  Poll(NULL,NULL);
  puts("PASS: MUSB EP0 regression + boot log INFO stream, TX backpressure, immutable snapshot, malformed command, reset, empty log and exit (mock MMIO + ASan/UBSan)");
}
