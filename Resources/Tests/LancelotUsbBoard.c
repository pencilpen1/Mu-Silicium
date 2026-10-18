/* Scripted I2C v2 tests of the actual lancelot board driver.
 * SPDX-License-Identifier: GPL-2.0-only
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#undef NULL
#include "Silicon/MediaTek/MediaTekPkg/Drivers/MusbHostDxe/LancelotUsb.c"

EFI_GUID gMediaTekClockProtocolGuid;
static EFI_BOOT_SERVICES Services;
EFI_BOOT_SERVICES *gBS=&Services;
static UINT16 IRegs[128];
static UINT8 ChipRegs[128][256], FifoData[8], RxByte;
static UINTN FifoLen, ClockEnables, ChargeWrites, WaitUs;
static UINT16 FailStatus;
static BOOLEAN Stuck, BadCount;
static UINT64 Freq=124800000;
static UINT32 Mux175=0xAAAAAAAA, Mux176=0x55555555;
VOID EFIAPI MemoryFence(VOID) {}
VOID EFIAPI BootLogPrint(CONST CHAR8 *Format, ...) {}
UINTN EFIAPI MicroSecondDelay(UINTN Us) { WaitUs+=Us; return Us; }
UINT16 EFIAPI MmioRead16(UINTN A) {
  assert(A>=I2C7_BASE && A<I2C7_BASE+256);
  return IRegs[(A-I2C7_BASE)/2];
}
UINT16 EFIAPI MmioWrite16(UINTN A,UINT16 V) {
  assert(A>=I2C7_BASE && A<I2C7_BASE+256);
  UINTN Offset=A-I2C7_BASE;
  if(Offset==I2C_STATUS) IRegs[Offset/2]&=~V;
  else IRegs[Offset/2]=V;
  if(Offset==I2C_FIFO_CLEAR) FifoLen=0;
  if(Offset==I2C_START) {
    // Firmware must use PIO with a repeated START for combined byte reads.
    UINT8 Addr=IRegs[I2C_ADDR/2]>>1, Reg=FifoData[0];
    assert(Addr==TYPEC_ADDR || Addr==CHARGER_ADDR);
    assert(!(IRegs[I2C_CONTROL/2]&BIT2));
    if(Stuck) return V;
    IRegs[I2C_STATUS/2]=FailStatus ? FailStatus : I2C_COMPLETE;
    if(FailStatus) return V;
    if(IRegs[I2C_CONTROL/2]&BIT4) {
      assert(IRegs[I2C_CONTROL/2]&BIT1);
      assert(IRegs[I2C_LENGTH/2]==1 && IRegs[I2C_AUX/2]==1 && IRegs[I2C_TRANSACTIONS/2]==2);
      assert(FifoLen==1); RxByte=ChipRegs[Addr][Reg];
      if(Addr==TYPEC_ADDR && Reg==3) ChipRegs[Addr][Reg]=0; // read-to-clear
      IRegs[I2C_FIFO_STAT/2]=BadCount ? 0x20:0x10;
    } else {
      assert(FifoLen==2 && IRegs[I2C_LENGTH/2]==2 && IRegs[I2C_TRANSACTIONS/2]==1);
      ChipRegs[Addr][Reg]=FifoData[1];
      if(Addr==CHARGER_ADDR) ChargeWrites++;
    }
  }
  return V;
}
UINT8 EFIAPI MmioWrite8(UINTN A,UINT8 V) {
  assert(A==I2C7_BASE && FifoLen<8); FifoData[FifoLen++]=V; return V;
}
UINT8 EFIAPI MmioRead8(UINTN A) { assert(A==I2C7_BASE); return RxByte; }
UINT32 EFIAPI MmioRead32(UINTN A) {
  if(A==0x10005450) return Mux175;
  if(A==0x10005460) return Mux176;
  assert(A==I2C7_DMA+8); return 0;
}
UINT32 EFIAPI MmioWrite32(UINTN A,UINT32 V) {
  if(A==0x10005450) Mux175=V;
  else if(A==0x10005460) Mux176=V;
  else assert(A==I2C7_DMA+12);
  return V;
}
UINT32 EFIAPI MmioAndThenOr32(UINTN A,UINT32 And,UINT32 Or) {
  return MmioWrite32(A,(MmioRead32(A)&And)|Or);
}
static EFI_STATUS EFIAPI GetId(CONST CHAR8 *Name, UINT32 *Id) {
  assert(!strcmp(Name,"INFRA_I2C_AP") || !strcmp(Name,"INFRA_AP_DMA"));
  *Id=!strcmp(Name,"INFRA_I2C_AP") ? 1:2; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Enable(UINT32 Id,BOOLEAN On) { assert(On); ClockEnables++; return 0; }
static EFI_STATUS EFIAPI Frequency(UINT32 Id,UINT64 *Value) { *Value=Freq; return 0; }
static MTK_CLOCK_PROTOCOL Clock={.GetId=GetId,.SetEnable=Enable,.GetFrequency=Frequency};
static EFI_STATUS EFIAPI Locate(EFI_GUID *Guid,VOID *Registration,VOID **Out) {
  assert(Guid==&gMediaTekClockProtocolGuid); *Out=&Clock; return 0;
}
int main(void) {
  Services.LocateProtocol=Locate;
  ChipRegs[TYPEC_ADDR][1]=6; ChipRegs[TYPEC_ADDR][2]=0x24;
  ChipRegs[CHARGER_ADDR][0x32]=0x12; ChipRegs[CHARGER_ADDR][0x0A]=0xA9;
  assert(LancelotUsbBoardInit()==0 && ClockEnables==2);
  assert(Mux175==0x9AAAAAAA && Mux176==0x55555551);
  assert(ChipRegs[TYPEC_ADDR][2]==0x05 && IRegs[I2C_DIV/2]==0x0404);
  assert(IRegs[I2C_HS/2]==0 && IRegs[I2C_IO/2]==3 && IRegs[I2C_TIMING/2]);
  UINTN Old=ChargeWrites;
  ChipRegs[TYPEC_ADDR][4]=8; // PC/source attached: never source VBUS
  assert(LancelotUsbVbus(TRUE)==EFI_NOT_READY && ChargeWrites==Old);
  ChipRegs[TYPEC_ADDR][4]=4; // sink attached
  assert(LancelotUsbVbus(TRUE)==0 && ChipRegs[CHARGER_ADDR][0x32]==0x13);
  assert(ChipRegs[CHARGER_ADDR][0x0A]==0xA9); // no uninitialized current-limit encoding
  assert(LancelotUsbVbus(FALSE)==0 && ChipRegs[CHARGER_ADDR][0x32]==0x12);
  MTK_USB_ROLE Role; UINT8 Events, RawStatus;
  ChipRegs[TYPEC_ADDR][3]=3; ChipRegs[TYPEC_ADDR][4]=8;
  assert(LancelotUsbGetRole(&Role,&Events,&RawStatus)==0 && Role==MtkUsbRoleDevice && Events==3 && RawStatus==8);
  assert(LancelotUsbGetRole(&Role,&Events,&RawStatus)==0 && Events==0);
  ChipRegs[TYPEC_ADDR][4]=4;
  assert(LancelotUsbGetRole(&Role,&Events,&RawStatus)==0 && Role==MtkUsbRoleHost && RawStatus==4);
  ChipRegs[TYPEC_ADDR][4]=0;
  assert(LancelotUsbGetRole(&Role,&Events,&RawStatus)==0 && Role==MtkUsbRoleNone && RawStatus==0);
  ChipRegs[TYPEC_ADDR][4]=0xFF;
  assert(LancelotUsbGetRole(&Role,&Events,&RawStatus)==EFI_DEVICE_ERROR && Role==MtkUsbRoleNone);
  UINT8 Value=0xEE;
  FailStatus=I2C_COMPLETE|BIT1;
  assert(I2cByte(TYPEC_ADDR,1,&Value,TRUE)==EFI_DEVICE_ERROR && Value==0xEE);
  FailStatus=0; Stuck=TRUE; WaitUs=0;
  assert(I2cByte(TYPEC_ADDR,1,&Value,TRUE)==EFI_TIMEOUT && WaitUs<6000);
  Stuck=FALSE; BadCount=TRUE;
  assert(I2cByte(TYPEC_ADDR,1,&Value,TRUE)==EFI_DEVICE_ERROR);
  BadCount=FALSE;
  assert(I2cTiming(0)==EFI_UNSUPPORTED && I2cTiming(26000000)==0);
  LancelotUsbBoardExit();
  assert(ChipRegs[TYPEC_ADDR][2]==0x24 && !(ChipRegs[CHARGER_ADDR][0x32]&1));
  assert(LancelotUsbVbus(TRUE)==EFI_NOT_READY);
  puts("PASS: I2C7 v2 PIO/readback, NACK/timeout/FIFO errors, WUSB3801 role and SMB1351 masked VBUS writes (ASan/UBSan)");
}
