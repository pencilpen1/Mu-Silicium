/** @file
  lancelot board support: MT6768 I2C7 v2, WUSB3801 and SMB1351 VBUS.
  PIO SMBus byte transactions, bounded waits, no DMA buffers.
  SPDX-License-Identifier: GPL-2.0-only
**/
#include "MusbHost.h"
#include <Protocol/MtkClock.h>

#define I2C7_BASE      0x11004000U
#define I2C7_DMA       0x11000680U
#define I2C_DATA       0x00
#define I2C_ADDR       0x04
#define I2C_MASK       0x08
#define I2C_STATUS     0x0C
#define I2C_CONTROL    0x10
#define I2C_LENGTH     0x14
#define I2C_TRANSACTIONS 0x18
#define I2C_DELAY      0x1C
#define I2C_TIMING     0x20
#define I2C_START      0x24
#define I2C_EXT        0x28
#define I2C_LTIMING    0x2C
#define I2C_HS         0x30
#define I2C_IO         0x34
#define I2C_FIFO_CLEAR 0x38
#define I2C_MCU_INTR   0x40
#define I2C_AUX        0x44
#define I2C_DIV        0x48
#define I2C_TIMEOUT    0x4C
#define I2C_RESET      0x50
#define I2C_FIFO_STAT  0xF4
#define I2C_COMPLETE   BIT0
#define I2C_ERRORS     (BIT1 | BIT2 | BIT3 | BIT5 | BIT6 | BIT8)
#define TYPEC_ADDR     0x60
#define CHARGER_ADDR   0x55

STATIC UINT16 mTiming, mLowTiming, mTimeout;
STATIC UINT8 mOriginalControl;
STATIC BOOLEAN mBoardReady;

STATIC VOID Iw (UINTN Reg, UINT16 Value) { MmioWrite16 (I2C7_BASE + Reg, Value); }
STATIC UINT16 Ir (UINTN Reg) { return MmioRead16 (I2C7_BASE + Reg); }

STATIC
EFI_STATUS
I2cTiming (UINT64 SourceHz)
{
  // DT clock-div=5, 100 kHz standard mode. MT6768 cnt_constraint means
  // sample=1 encodes step directly; otherwise both divisors encode minus one.
  UINTN Divisor, Sample, Step;
  if ((SourceHz < 1000000) || (SourceHz > 1000000000)) return EFI_UNSUPPORTED;
  SourceHz /= 5;
  Divisor = (UINTN)((SourceHz + 199999) / 200000);
  for (Sample = 1; Sample <= 8; Sample++) {
    Step = (Divisor + Sample - 1) / Sample;
    if ((Step == 0) || (Step > 64) || ((Sample == 1) && (Step == 64))) continue;
    mTiming = (UINT16)(((Sample - 1) << 8) | (Step - (Sample != 1)) | BIT0);
    mLowTiming = (UINT16)(((Sample - 1) << 6) | (Step - (Sample != 1)));
    mTimeout = (UINT16)MIN (65535, (SourceHz / 500 + Sample * Step - 1) / (Sample * Step));
    return EFI_SUCCESS;
  }
  return EFI_UNSUPPORTED;
}

STATIC
VOID
I2cReset (VOID)
{
  Iw (I2C_RESET, 1);
  MicroSecondDelay (10);
  Iw (I2C_MASK, 0);
  Iw (I2C_MCU_INTR, 0);
  Iw (I2C_STATUS, 0x1FF);
  Iw (I2C_FIFO_CLEAR, 1);
  Iw (I2C_DIV, 0x0404);
  Iw (I2C_TIMING, mTiming);
  Iw (I2C_LTIMING, mLowTiming);
  Iw (I2C_TIMEOUT, mTimeout);
  Iw (I2C_EXT, 0x8001);
  Iw (I2C_HS, 0);
  Iw (I2C_IO, 3); // open drain, as specified by the lancelot DT
  Iw (I2C_DELAY, 10);
}

STATIC
EFI_STATUS
I2cByte (UINT8 Address, UINT8 Reg, UINT8 *Value, BOOLEAN Read)
{
  UINT16 Status = 0;
  UINTN Count;
  if (Value == NULL) return EFI_INVALID_PARAMETER;
  // ACK detection + clock stretching + MT6768 completion IRQ selection.
  Iw (I2C_CONTROL, BIT5 | BIT3 | BIT7 | (Read ? BIT4 | BIT1 : 0));
  Iw (I2C_ADDR, (UINT16)Address << 1);
  Iw (I2C_STATUS, 0x1FF);
  Iw (I2C_FIFO_CLEAR, 1);
  Iw (I2C_LENGTH, Read ? 1 : 2);
  Iw (I2C_AUX, Read ? 1 : 0);
  Iw (I2C_TRANSACTIONS, Read ? 2 : 1);
  MmioWrite8 (I2C7_BASE + I2C_DATA, Reg);
  if (!Read) MmioWrite8 (I2C7_BASE + I2C_DATA, *Value);
  MemoryFence ();
  Iw (I2C_START, 1);
  for (Count = 0; Count < 500; Count++) {
    Status = Ir (I2C_STATUS);
    if (Status & (I2C_COMPLETE | I2C_ERRORS)) break;
    MicroSecondDelay (10);
  }
  if ((Count == 500) || (Status & I2C_ERRORS)) {
    I2cReset ();
    return Count == 500 ? EFI_TIMEOUT : EFI_DEVICE_ERROR;
  }
  if (Read) {
    if (((Ir (I2C_FIFO_STAT) >> 4) & 0xF) != 1) {
      I2cReset ();
      return EFI_DEVICE_ERROR;
    }
    *Value = MmioRead8 (I2C7_BASE + I2C_DATA);
  }
  Iw (I2C_STATUS, Status);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ModifyByte (UINT8 Address, UINT8 Reg, UINT8 Mask, UINT8 Value)
{
  UINT8 Old, Check;
  EFI_STATUS Status = I2cByte (Address, Reg, &Old, TRUE);
  if (EFI_ERROR (Status)) return Status;
  Old = (Old & ~Mask) | (Value & Mask);
  Status = I2cByte (Address, Reg, &Old, FALSE);
  if (EFI_ERROR (Status)) return Status;
  Status = I2cByte (Address, Reg, &Check, TRUE);
  if (EFI_ERROR (Status)) return Status;
  return (Check & Mask) == (Old & Mask) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS
LancelotUsbPartnerIsSink (BOOLEAN *IsSink)
{
  UINT8 StatusByte;
  EFI_STATUS Status;
  if (IsSink == NULL) return EFI_INVALID_PARAMETER;
  *IsSink = FALSE;
  if (!mBoardReady) return EFI_NOT_READY;
  Status = I2cByte (TYPEC_ADDR, 0x04, &StatusByte, TRUE);
  if (!EFI_ERROR (Status)) *IsSink = (StatusByte & 0x1C) == 0x04;
  return Status;
}

EFI_STATUS
LancelotUsbGetRole (MTK_USB_ROLE *Role, UINT8 *Events, UINT8 *RawStatus)
{
  UINT8 StatusByte;
  EFI_STATUS Status;
  if (Role == NULL || Events == NULL || RawStatus == NULL) return EFI_INVALID_PARAMETER;
  *Role = MtkUsbRoleNone; *Events = 0; *RawStatus = 0xFF;
  if (!mBoardReady) return EFI_NOT_READY;
  // Match the reference driver's read-to-clear interrupt -> status sequence.
  // A latched detach matters even if an attach arrived within the same poll.
  Status = I2cByte (TYPEC_ADDR, 0x03, Events, TRUE);
  if (EFI_ERROR (Status)) return Status;
  Status = I2cByte (TYPEC_ADDR, 0x04, &StatusByte, TRUE);
  if (EFI_ERROR (Status)) return Status;
  *RawStatus = StatusByte;
  if (StatusByte == 0xFF) return EFI_DEVICE_ERROR;
  if ((StatusByte & 0x1C) == 0x04) *Role = MtkUsbRoleHost;
  else if ((StatusByte & 0x1C) == 0x08) *Role = MtkUsbRoleDevice;
  return EFI_SUCCESS;
}

EFI_STATUS
LancelotUsbVbus (BOOLEAN Enable)
{
  EFI_STATUS Status;
  BOOLEAN Sink;
  if (!mBoardReady) return EFI_NOT_READY;
  if (Enable) {
    Status = LancelotUsbPartnerIsSink (&Sink);
    if (EFI_ERROR (Status)) return Status;
    if (!Sink) return EFI_NOT_READY;
  }
  // Command register, not a volatile configuration register. Preserve charging
  // policy and LK's calibrated current limit. Do not port the Linux 500mA path
  // which uses an uninitialized encoding in smb1351_set_otg_current().
  return ModifyByte (CHARGER_ADDR, 0x32, BIT0, Enable ? BIT0 : 0);
}

EFI_STATUS
LancelotUsbBoardInit (VOID)
{
  MTK_CLOCK_PROTOCOL *Clock;
  UINT64 Frequency;
  UINT32 Id;
  UINT8 Chip, Control, Limit;
  EFI_STATUS Status;
  Status = gBS->LocateProtocol (&gMediaTekClockProtocolGuid, NULL, (VOID **)&Clock);
  if (EFI_ERROR (Status)) return Status;
  Status = Clock->GetId ("INFRA_I2C_AP", &Id);
  if (EFI_ERROR (Status)) return Status;
  Status = Clock->SetEnable (Id, TRUE);
  if (EFI_ERROR (Status)) return Status;
  Status = Clock->GetFrequency (Id, &Frequency);
  if (EFI_ERROR (Status)) return Status;
  Status = I2cTiming (Frequency);
  if (EFI_ERROR (Status)) return Status;
  // GPIO175/176 mode 1 = SCL7/SDA7. RMW the actual mode register, not its
  // write-one SET/CLR aliases, preserving every adjacent pin's mux.
  MmioAndThenOr32 (0x10005450, ~(UINT32)(7U << 28), 1U << 28);
  MmioAndThenOr32 (0x10005460, ~(UINT32)7, 1);
  // Stop only I2C7's DMA channel before taking over its FIFO.
  Status = Clock->GetId ("INFRA_AP_DMA", &Id);
  if (EFI_ERROR (Status)) return Status;
  Status = Clock->SetEnable (Id, TRUE);
  if (EFI_ERROR (Status)) return Status;
  MmioWrite32 (I2C7_DMA + 0x0C, 2);
  MicroSecondDelay (10);
  MmioWrite32 (I2C7_DMA + 0x0C, 0);
  if (MmioRead32 (I2C7_DMA + 0x08) & 1) return EFI_DEVICE_ERROR;
  I2cReset ();
  Status = I2cByte (TYPEC_ADDR, 0x01, &Chip, TRUE);
  if (EFI_ERROR (Status)) return Status;
  if ((Chip & 7) != 6) return EFI_UNSUPPORTED;
  Status = I2cByte (TYPEC_ADDR, 0x02, &mOriginalControl, TRUE);
  if (EFI_ERROR (Status)) return Status;
  Status = I2cByte (CHARGER_ADDR, 0x0A, &Limit, TRUE);
  if (EFI_ERROR (Status)) return Status;
  mBoardReady = TRUE;
  Status = LancelotUsbVbus (FALSE);
  if (EFI_ERROR (Status)) return Status;
  // DRP, default USB current advertisement, masked interrupt (status polling).
  // Host VBUS is enabled only after a sink partner has actually been detected.
  Control = 0x05;
  Status = I2cByte (TYPEC_ADDR, 0x02, &Control, FALSE);
  BootLogPrint ("[HOST] I2C7 clk=%Lu WUSB3801=%02x OTG-limit=%02x status=%r\n",
                Frequency, Chip, Limit & 0x0C, Status);
  return Status;
}

VOID
LancelotUsbBoardExit (VOID)
{
  if (!mBoardReady) return;
  LancelotUsbVbus (FALSE);
  I2cByte (TYPEC_ADDR, 0x02, &mOriginalControl, FALSE);
  mBoardReady = FALSE;
}
