/** @file
  lancelot MUSB PIO peripheral, full-speed, diagnostic getvar and boot log.

  Register definitions/sequencing checked against MediaTek Linux usb20/mt6768,
  phy-mtk-tphy and clk-mt6768. Reference commits are recorded in README.md.
  SPDX-License-Identifier: GPL-2.0-only
**/
#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BootLogLib.h>

#define MAC       0x11200000U
#define PHY       0x11cc0800U
#define FADDR     0x00
#define POWER     0x01
#define INTRTX    0x02
#define INTRRX    0x04
#define INTRTXE   0x06
#define INTRRXE   0x08
#define INTRUSB   0x0a
#define INTRUSBE  0x0b
#define INDEX     0x0e
#define TXMAXP    0x10
#define TXCSR     0x12
#define RXMAXP    0x14
#define RXCSR     0x16
#define RXCOUNT   0x18
#define FIFO0     0x20
#define FIFO1     0x24
#define DEVCTL    0x60
#define TXFIFOSZ  0x62
#define RXFIFOSZ  0x63
#define TXFIFOADD 0x64
#define RXFIFOADD 0x66
#define SOFTCONN  0x40
#define RESET_IRQ 0x04

#define EP0_RX       0x01
#define EP0_TX       0x02
#define EP0_STALLED  0x04
#define EP0_END      0x08
#define EP0_SETUPEND 0x10
#define EP0_STALL    0x20
#define EP0_ACK_RX   0x40
#define EP0_ACK_END  0x80

STATIC EFI_EVENT mPollEvent, mExitEvent;
STATIC BOOLEAN mStarted, mStopped, mActive;
STATIC UINT8 mConfiguration, mAddress;
STATIC INTN mPendingAddress = -1;
STATIC UINT32 mStage, mResets, mSetups, mCommands, mStalls;
STATIC UINT32 mLastSetup;
STATIC UINT32 mLastSetupIndexLength;
STATIC UINT32 mControlPackets, mControlCompletions, mSetupAckTimeouts;
STATIC UINT16 mLastControlCsr, mQueuedControlCsr;
STATIC UINT16 mLastBulkRxCsr, mLastBulkTxCsr, mLastBulkRxCount;
STATIC UINT16 mLastBulkInterrupt;
STATIC UINTN mSetupAckPolls;
STATIC UINT16 mHardwareVersion;
STATIC EFI_STATUS mInitStatus;
STATIC UINT8 mControl[128];
STATIC UINTN mControlLength, mControlOffset;
STATIC BOOLEAN mControlZlp;
typedef enum {
  ControlIdle,
  ControlDataIn,
  ControlStatusIn,
  ControlStatusOut,
  ControlSetupAck
} CONTROL_STATE;
STATIC CONTROL_STATE mControlState;
STATIC BOOLEAN mHaltIn, mHaltOut;

// A snapshot is immutable while its INFO packets are sent. Boot log writers
// never wait for USB, and a bus reset cancels only the transfer, not the log.
#define LOG_SNAPSHOT_SIZE  4096U
STATIC UINT8 mLogSnapshot[LOG_SNAPSHOT_SIZE];
STATIC UINTN mLogLength, mLogOffset;
STATIC UINT64 mLogBootId, mLogStart;
typedef enum { LogReplyNone, LogReplyHeader, LogReplyData, LogReplyEnd } LOG_REPLY;
STATIC LOG_REPLY mLogReply;

// Full-speed-only device: deliberately do not set POWER.HSENAB. Descriptor
// qualifier/other-speed requests are stalled as required for FS-only devices.
STATIC CONST UINT8 mDevice[] = {
  18, 1, 0x00, 0x02, 0, 0, 0, 64, 0xd1, 0x18, 0x0d, 0xd0,
  0x03, 0x00, 1, 2, 3, 1
};
STATIC CONST UINT8 mConfig[] = {
  9, 2, 32, 0, 1, 1, 0, 0xc0, 1,
  9, 4, 0, 0, 2, 0xff, 0x42, 3, 0,
  7, 5, 0x81, 2, 64, 0, 0,
  7, 5, 0x01, 2, 64, 0, 0
};

STATIC UINT8 R8 (UINTN Reg) { return MmioRead8 (MAC + Reg); }
STATIC UINT16 R16 (UINTN Reg) { return MmioRead16 (MAC + Reg); }
STATIC VOID W8 (UINTN Reg, UINT8 V) { MmioWrite8 (MAC + Reg, V); }
STATIC VOID W16 (UINTN Reg, UINT16 V) { MmioWrite16 (MAC + Reg, V); }

STATIC
VOID
FifoRead (UINTN Reg, UINT8 *Buffer, UINTN Count)
{
  for (UINTN I = 0; I < Count; I++) {
    Buffer[I] = R8 (Reg);
  }
}

STATIC
VOID
FifoWrite (UINTN Reg, CONST UINT8 *Buffer, UINTN Count)
{
  for (UINTN I = 0; I < Count; I++) {
    W8 (Reg, Buffer[I]);
  }
}

STATIC
VOID
ResetBulk (VOID)
{
  W8 (INDEX, 1);
  // EP0 owns FIFO bytes 0..63, EP1 IN 64..127, EP1 OUT 128..191.
  W8 (TXFIFOSZ, 3);
  W8 (RXFIFOSZ, 3);
  W16 (TXFIFOADD, 8);
  W16 (RXFIFOADD, 16);
  W16 (TXMAXP, 64);
  W16 (RXMAXP, 64);
  W16 (TXCSR, 0x2048); // mode TX, flush, clear toggle; no DMA
  W16 (TXCSR, 0x2048);
  W16 (RXCSR, 0x0090); // flush, clear toggle; no DMA
  W16 (RXCSR, 0x0090);
  W16 (TXCSR, 0x2000);
  W16 (RXCSR, 0);
  mHaltIn = mHaltOut = FALSE;
  mLogReply = LogReplyNone;
  W8 (INDEX, 0);
}

STATIC
VOID
BusReset (VOID)
{
  mResets++;
  mConfiguration = mAddress = 0;
  mPendingAddress = -1;
  mControlState = ControlIdle;
  mSetupAckPolls = 0;
  mControlLength = mControlOffset = 0;
  mControlZlp = FALSE;
  W8 (FADDR, 0);
  ResetBulk ();
  W16 (TXCSR, 0x100); // EP0 flush
  mStage = 4;
}

STATIC
VOID
ControlStall (VOID)
{
  mControlState = ControlIdle;
  mPendingAddress = -1;
  mStalls++;
  W16 (TXCSR, EP0_ACK_RX | EP0_STALL);
}

STATIC
VOID
ControlPacket (VOID)
{
  UINTN N = MIN (64, mControlLength - mControlOffset);
  BOOLEAN Last;

  FifoWrite (FIFO0, mControl + mControlOffset, N);
  mControlOffset += N;
  Last = (mControlOffset == mControlLength) && (!mControlZlp || (N == 0));
  if (Last) {
    mControlState = ControlStatusOut;
  } else {
    mControlState = ControlDataIn;
  }
  W16 (TXCSR, EP0_TX | (Last ? EP0_END : 0));
  mQueuedControlCsr = R16 (TXCSR);
  mControlPackets++;
}

STATIC
VOID
ControlReply (CONST UINT8 *Data, UINTN Length, UINT16 Requested)
{
  mControlLength = MIN (Length, Requested);
  if (mControlLength > sizeof (mControl)) {
    ControlStall ();
    return;
  }
  mControlOffset = 0;
  mControlZlp = (Length < Requested) && ((mControlLength % 64) == 0);
  if (Data != mControl) {
    CopyMem (mControl, Data, mControlLength);
  }
  // EP0 shares RX/TX FIFO storage. First release the SETUP receive packet,
  // then wait for RXPKTRDY to clear before loading any IN bytes. Combining
  // ACK_RX with TXPKTRDY after writing the FIFO violates that handover.
  // MediaTek musb_read_setup() performs the same ACK/readback before TX.
  mControlState = ControlSetupAck;
  mSetupAckPolls = 0;
  W16 (TXCSR, EP0_ACK_RX);
}

STATIC
VOID
ControlAck (VOID)
{
  mControlState = ControlStatusIn;
  W16 (TXCSR, EP0_ACK_RX | EP0_END);
}

STATIC
VOID
Setup (CONST UINT8 *S)
{
  UINT8 Type = S[0], Request = S[1];
  UINT16 Value = S[2] | (S[3] << 8);
  UINT16 IndexValue = S[4] | (S[5] << 8);
  UINT16 Length = S[6] | (S[7] << 8);
  UINT8 Reply[2] = { 0, 0 };

  mSetups++;
  mLastSetup = ((UINT32)Type << 24) | ((UINT32)Request << 16) | Value;
  mLastSetupIndexLength = ((UINT32)IndexValue << 16) | Length;
  mPendingAddress = -1;
  if ((Type == 0x80) && (Request == 6) && (Length != 0)) {
    UINT8 Kind = Value >> 8, Number = Value & 255;
    if ((Kind == 1) && (Number == 0) && (IndexValue == 0)) {
      ControlReply (mDevice, sizeof (mDevice), Length);
      return;
    }
    if ((Kind == 2) && (Number == 0) && (IndexValue == 0)) {
      ControlReply (mConfig, sizeof (mConfig), Length);
      return;
    }
    if (Kind == 3) {
      CONST CHAR8 *Text = NULL;
      if (Number == 0) {
        CONST UINT8 Lang[] = { 4, 3, 9, 4 };
        ControlReply (Lang, sizeof (Lang), Length);
        return;
      }
      if ((IndexValue == 0x0409) || (IndexValue == 0)) {
        if (Number == 1) Text = "Lancelot UEFI";
        if (Number == 2) Text = "MUSB diagnostics";
        if (Number == 3) Text = "lancelot-uefi";
      }
      if (Text != NULL) {
        UINTN N = AsciiStrLen (Text);
        mControl[0] = (UINT8)(2 + N * 2);
        mControl[1] = 3;
        for (UINTN I = 0; I < N; I++) {
          mControl[2 + I * 2] = Text[I];
          mControl[3 + I * 2] = 0;
        }
        ControlReply (mControl, mControl[0], Length);
        return;
      }
    }
  } else if ((Type == 0) && (Request == 5) && (Value <= 127) &&
             (IndexValue == 0) && (Length == 0) && (mConfiguration == 0)) {
    mPendingAddress = Value; // must be applied only after status-IN ACK
    ControlAck ();
    return;
  } else if ((Type == 0) && (Request == 9) && (Value <= 1) &&
             (IndexValue == 0) && (Length == 0) && (mAddress != 0)) {
    mConfiguration = (UINT8)Value;
    ResetBulk ();
    mStage = Value ? 6 : 5;
    ControlAck ();
    return;
  } else if ((Type == 0x80) && (Request == 8) && (Value == 0) &&
             (IndexValue == 0) && (Length == 1)) {
    ControlReply (&mConfiguration, 1, Length);
    return;
  } else if ((Request == 0) && (Value == 0) && (Length == 2)) {
    if ((Type == 0x80) && (IndexValue == 0)) {
      Reply[0] = 1; // self-powered
    } else if ((Type == 0x81) && (IndexValue == 0) && mConfiguration) {
      Reply[0] = 0;
    } else if ((Type == 0x82) && ((IndexValue == 0) || (IndexValue == 0x80))) {
      Reply[0] = 0;
    } else if ((Type == 0x82) && mConfiguration &&
               ((IndexValue == 1) || (IndexValue == 0x81))) {
      Reply[0] = IndexValue == 1 ? mHaltOut : mHaltIn;
    } else {
      ControlStall ();
      return;
    }
    ControlReply (Reply, 2, Length);
    return;
  } else if ((Type == 0x81) && (Request == 10) && (Value == 0) &&
             (IndexValue == 0) && (Length == 1) && mConfiguration) {
    ControlReply (Reply, 1, Length);
    return;
  } else if ((Type == 1) && (Request == 11) && (Value == 0) &&
             (IndexValue == 0) && (Length == 0) && mConfiguration) {
    ResetBulk ();
    ControlAck ();
    return;
  } else if ((Type == 2) && ((Request == 1) || (Request == 3)) &&
             (Value == 0) && (Length == 0) && mConfiguration &&
             ((IndexValue == 1) || (IndexValue == 0x81))) {
    BOOLEAN Halt = Request == 3;
    W8 (INDEX, 1);
    if (IndexValue == 0x81) {
      mHaltIn = Halt;
      W16 (TXCSR, Halt ? 0x2010 : 0x2048);
    } else {
      mHaltOut = Halt;
      W16 (RXCSR, Halt ? 0x0020 : 0x0090);
    }
    W8 (INDEX, 0);
    ControlAck ();
    return;
  }
  ControlStall ();
}

STATIC
VOID
PollControl (BOOLEAN TxInterrupt)
{
  UINT16 Csr;
  W8 (INDEX, 0);
  Csr = R16 (TXCSR);
  // Preserve the last active transaction even if Windows ends its retries
  // with another bus reset. Idle polls must not erase the failure evidence.
  if (TxInterrupt || (Csr != 0) || (mControlState != ControlIdle)) {
    mLastControlCsr = Csr;
  }
  if ((Csr & EP0_STALLED) != 0) {
    W16 (TXCSR, 0);
    mControlState = ControlIdle;
    mPendingAddress = -1;
    Csr = R16 (TXCSR);
  }
  if ((Csr & EP0_SETUPEND) != 0) {
    W16 (TXCSR, EP0_ACK_END);
    mControlState = ControlIdle;
    mPendingAddress = -1;
    Csr = R16 (TXCSR);
  }
  // DATAEND remains set until the controller has completed the status
  // handshake. A pending/coalesced IRQ alone is not a completed SET_ADDRESS.
  if ((Csr & EP0_END) != 0) {
    return;
  }
  if (mControlState == ControlSetupAck) {
    if ((Csr & EP0_RX) != 0) {
      // Bounded non-blocking wait: at most 100 poll callbacks (~100 ms).
      if (++mSetupAckPolls >= 100) {
        mSetupAckTimeouts++;
        ControlStall ();
      }
      return;
    }
    if ((Csr & EP0_TX) == 0) {
      ControlPacket ();
    }
    return;
  }
  if (TxInterrupt && (mControlState == ControlStatusIn) && !(Csr & EP0_TX)) {
    if (mPendingAddress >= 0) {
      mAddress = (UINT8)mPendingAddress;
      W8 (FADDR, mAddress);
      mPendingAddress = -1;
      mStage = 5;
    }
    mControlState = ControlIdle;
    mControlCompletions++;
  } else if (TxInterrupt && (mControlState == ControlStatusOut) && !(Csr & EP0_TX)) {
    // MUSB normally consumes the status OUT ZLP itself; no RXPKTRDY ZLP
    // is required. The next SETUP can already be pending in this same IRQ.
    mControlState = ControlIdle;
    mControlCompletions++;
    if (((Csr & EP0_RX) != 0) && (R8 (RXCOUNT) == 0)) {
      W16 (TXCSR, EP0_ACK_RX);
      return;
    }
  }
  if ((Csr & EP0_RX) != 0) {
    // COUNT0 is an 8-bit register, unlike bulk RXCOUNT.
    UINTN Count = R8 (RXCOUNT);
    if ((mControlState == ControlStatusOut) && (Count == 0)) {
      W16 (TXCSR, EP0_ACK_RX);
      mControlState = ControlIdle;
      mControlCompletions++;
    } else if (Count == 8) {
      UINT8 Packet[8];
      FifoRead (FIFO0, Packet, sizeof (Packet));
      Setup (Packet);
    } else {
      ControlStall ();
    }
  } else if (TxInterrupt && (mControlState == ControlDataIn) && !(Csr & EP0_TX)) {
    ControlPacket ();
  }
}

STATIC
CONST CHAR8 *
GetReply (CONST CHAR8 *Command)
{
  if (AsciiStrCmp (Command, "getvar:version") == 0) return "OKAY0.4";
  if (AsciiStrCmp (Command, "getvar:product") == 0) return "OKAYlancelot-uefi";
  if (AsciiStrCmp (Command, "getvar:serialno") == 0) return "OKAYlancelot-uefi";
  if (AsciiStrCmp (Command, "getvar:version-bootloader") == 0) return "OKAYmusb-pio-diag-0.3";
  if (AsciiStrCmp (Command, "getvar:log-version") == 0) return "OKAYLLOG1";
  if (AsciiStrCmp (Command, "getvar:max-download-size") == 0) return "OKAY0x00000000";
  return "FAILdiagnostic getvar/oem log only";
}

STATIC
VOID
Hex64 (CHAR8 *Text, UINT64 Value)
{
  STATIC CONST CHAR8 Hex[] = "0123456789abcdef";
  for (UINTN I = 0; I < 16; I++) Text[I] = Hex[(Value >> (60 - I * 4)) & 15];
}

STATIC
BOOLEAN
ParseLogCursor (CONST CHAR8 *Command, UINTN Length, UINT64 *Cursor)
{
  CONST CHAR8 Prefix[] = "oem log ";
  if (Length != sizeof (Prefix) - 1 + 16) return FALSE;
  for (UINTN I = 0; I < sizeof (Prefix) - 1; I++) {
    if (Command[I] != Prefix[I]) return FALSE;
  }
  *Cursor = 0;
  for (UINTN I = sizeof (Prefix) - 1; I < Length; I++) {
    UINT8 C = (UINT8)Command[I];
    UINT8 Digit;
    if ((C >= '0') && (C <= '9')) Digit = C - '0';
    else if ((C >= 'a') && (C <= 'f')) Digit = C - 'a' + 10;
    else if ((C >= 'A') && (C <= 'F')) Digit = C - 'A' + 10;
    else return FALSE;
    *Cursor = (*Cursor << 4) | Digit;
  }
  return TRUE;
}

STATIC
VOID
SendLogPacket (VOID)
{
  CHAR8 Packet[64];
  UINTN Length;
  STATIC CONST CHAR8 Hex[] = "0123456789abcdef";

  // Called only with EP1 selected and TXPKTRDY clear. All replies fit in one
  // short (<64-byte) full-speed packet, including the fastboot INFO prefix.
  if (mLogReply == LogReplyHeader) {
    CopyMem (Packet, "INFOLLOG1 ", 10);
    Hex64 (Packet + 10, mLogBootId);
    Packet[26] = ' ';
    Hex64 (Packet + 27, mLogStart);
    Packet[43] = ' ';
    Hex64 (Packet + 44, mLogStart + mLogLength);
    Length = 60;
    mLogReply = LogReplyData;
  } else if ((mLogReply == LogReplyData) && (mLogOffset < mLogLength)) {
    UINTN Count = MIN (24, mLogLength - mLogOffset);
    CopyMem (Packet, "INFOLLOGD ", 10);
    for (UINTN I = 0; I < Count; I++) {
      UINT8 Byte = mLogSnapshot[mLogOffset++];
      Packet[10 + I * 2] = Hex[Byte >> 4];
      Packet[11 + I * 2] = Hex[Byte & 15];
    }
    Length = 10 + Count * 2;
    if (mLogOffset == mLogLength) mLogReply = LogReplyEnd;
  } else {
    CopyMem (Packet, "OKAY", 4);
    Length = 4;
    mLogReply = LogReplyNone;
  }
  FifoWrite (FIFO1, (CONST UINT8 *)Packet, Length);
  W16 (TXCSR, 0x2001);
}

STATIC
VOID
PollBulk (VOID)
{
  UINTN Count;
  CHAR8 Command[65];
  CONST CHAR8 *Reply;
  UINT64 LogCursor;
  UINT16 RxCsr;
  W8 (INDEX, 1);
  mLastBulkTxCsr = R16 (TXCSR);
  RxCsr = R16 (RXCSR);
  mLastBulkRxCsr = RxCsr;
  mLastBulkRxCount = R16 (RXCOUNT);
  if ((mConfiguration == 0) || mHaltIn || mHaltOut ||
      (mLastBulkTxCsr & 1)) {
    W8 (INDEX, 0);
    return;
  }
  if (mLogReply != LogReplyNone) {
    SendLogPacket ();
    W8 (INDEX, 0);
    return;
  }
  if (!(RxCsr & 1)) {
    W8 (INDEX, 0);
    return;
  }
  Count = mLastBulkRxCount;
  if (Count > 64) {
    W16 (RXCSR, 0x90);
    W8 (INDEX, 0);
    return;
  }
  FifoRead (FIFO1, (UINT8 *)Command, Count);
  Command[Count] = 0;
  W16 (RXCSR, 0);
  if (Count != 0) {
    mCommands++;
    if (ParseLogCursor (Command, Count, &LogCursor)) {
      mLogLength = BootLogSnapshot (LogCursor, &mLogBootId, &mLogStart,
                                    mLogSnapshot, sizeof (mLogSnapshot));
      mLogOffset = 0;
      mLogReply = LogReplyHeader;
      SendLogPacket ();
    } else {
      Reply = GetReply (Command);
      FifoWrite (FIFO1, (CONST UINT8 *)Reply, AsciiStrLen (Reply));
      W16 (TXCSR, 0x2001);
    }
    mStage = 7;
  }
  W8 (INDEX, 0);
}

STATIC
VOID
EFIAPI
Poll (EFI_EVENT Event, VOID *Context)
{
  UINT8 Usb;
  UINT16 Tx, Rx;
  if (!mActive || mStopped) return;
  Usb = R8 (INTRUSB);
  Tx = R16 (INTRTX);
  Rx = R16 (INTRRX);
  mLastBulkInterrupt = Rx;
  // MTK interrupt status registers are W1C (unlike some other MUSB variants).
  W8 (INTRUSB, Usb);
  W16 (INTRTX, Tx);
  W16 (INTRRX, Rx);
  if ((Usb & RESET_IRQ) != 0) {
    BusReset ();
    BootLogPrint ("[USB] bus reset\n");
    return;
  }
  PollControl ((Tx & 1) != 0);
  PollBulk ();
}

STATIC
EFI_STATUS
HardwareInit (VOID)
{
  UINT8 SavedIndex;
  mStage = 1;
  // UNIVPLL is shared. Do not reprogram an unknown PLL or PMIC power state.
  // LK normally leaves it enabled; expose a clean failure if that is not so.
  if ((MmioRead32 (0x1000c238) & BIT0) == 0) return EFI_NOT_READY;
  // USB_TOP mux: univpll3_d4, clear PDN, commit via CLK_CFG_UPDATE bit 22.
  MmioWrite32 (0x10000098, BIT23 | BIT16);
  MmioWrite32 (0x10000094, BIT16);
  MmioWrite32 (0x10000004, BIT22);
  // INFRA ICUSB gate: write-one-to-clear disable bit 8.
  MmioWrite32 (0x10001084, BIT8);
  MicroSecondDelay (50);
  if ((MmioRead32 (0x10001090) & BIT8) ||
      (MmioRead32 (0x10000090) & BIT23)) return EFI_DEVICE_ERROR;
  mStage = 2;

  // Match the MT6768 USB2 PHY USB/device transition. Preserve calibration
  // fields/efuse tuning; LK supplies VUSB/VIO18/VA12 in this first bring-up.
  MmioAnd32 (PHY + 0x68, ~(UINT32)(BIT26 | BIT18 | BIT30 | BIT31));
  MmioAnd32 (PHY + 0x6c, ~(UINT32)BIT16);
  MmioOr32 (PHY + 0x00, BIT5);
  MmioAnd32 (PHY + 0x14, ~(UINT32)BIT11);
  MmioAndThenOr32 (PHY + 0x20, ~(UINT32)(BIT9 | BIT8 | BIT16 | BIT17), BIT18);
  MmioAndThenOr32 (PHY + 0x18, ~(UINT32)BIT23, BIT20);
  MmioAnd32 (PHY + 0x68, ~(UINT32)(BIT23 | BIT21 | BIT20 | BIT19 | BIT18 |
                          BIT17 | (15U << 10) | BIT7 | BIT6 | BIT5 | BIT4 | BIT2));
  // Device active: force IDDIG=1, VBUS/AVALID/BVALID=1, SESSEND=0.
  // No host VBUS sourcing or charger boost programming.
  MmioAndThenOr32 (PHY + 0x6c, ~(UINT32)BIT4, 0x3f2f);
  MicroSecondDelay (800);

  W8 (POWER, 0); // detach, disable high-speed negotiation and suspend
  MicroSecondDelay (10000);
  MmioWrite32 (MAC + 0xa4, 0); // mask external IRQ: this driver polls
  W16 (INTRTXE, 0);
  W16 (INTRRXE, 0);
  W8 (INTRUSBE, 0);
  for (UINTN I = 0; I < 8; I++) {
    MmioWrite32 (MAC + 0x204 + I * 16, 0); // stop legacy DMA
  }
  MmioWrite32 (MAC + 0xb00, 0); // QMU queue enables off
  W8 (DEVCTL, 0); // no host session request
  W8 (0x0f, 0); // testmode off
  mHardwareVersion = R16 (0x6c);
  SavedIndex = R8 (INDEX);
  W8 (INDEX, 1);
  if ((R8 (INDEX) != 1) || (mHardwareVersion == 0xffff) || (mHardwareVersion == 0)) {
    W8 (INDEX, SavedIndex);
    return EFI_DEVICE_ERROR;
  }
  W8 (INDEX, 0);
  BusReset ();
  mResets = 0;
  W8 (INTRUSB, 0xff);
  W16 (INTRTX, 0xffff);
  W16 (INTRRX, 0xffff);
  // Enable status generation, while L1/GIC output stays masked.
  W16 (INTRTXE, 3);
  W16 (INTRRXE, 2);
  W8 (INTRUSBE, RESET_IRQ);
  mStage = 3;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
Start (EFI_EVENT Event, VOID *Context)
{
  if (mStarted) return;
  mStarted = TRUE;
  // This port has no SEC-side log producer yet; initialize the shared ring so
  // the USB log command remains well-defined and later DXE writers can attach.
  BootLogInitialize (0x4C414E43454C4F54ULL);
  BootLogPrint ("[USB] starting MUSB log transport on interface 0, EP01/EP81\n");
  mInitStatus = HardwareInit ();
  if (!EFI_ERROR (mInitStatus)) {
    mActive = TRUE;
    mInitStatus = gBS->SetTimer (mPollEvent, TimerPeriodic, 10000); // 1 ms
    if (EFI_ERROR (mInitStatus)) {
      mActive = FALSE;
    } else {
      W8 (POWER, SOFTCONN);
    }
  }
  BootLogSetUsbActive (mActive);
  BootLogPrint ("[USB] initialization status=%r\n", mInitStatus);
}

STATIC
VOID
EFIAPI
Stop (EFI_EVENT Event, VOID *Context)
{
  BootLogPrint ("[USB] ExitBootServices: detach\n");
  mStopped = TRUE;
  if (mActive) {
    W8 (POWER, 0);
    W16 (INTRTXE, 0);
    W16 (INTRRXE, 0);
    W8 (INTRUSBE, 0);
    mActive = FALSE;
  }
}

EFI_STATUS
EFIAPI
MusbDiagnosticEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Poll, NULL, &mPollEvent);
  if (EFI_ERROR (Status)) return Status;
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, Stop, NULL,
                              &gEfiEventExitBootServicesGuid, &mExitEvent);
  if (EFI_ERROR (Status)) goto Error;
  // Timer architectural protocol is a DEPEX: service USB during DXE rather
  // than waiting for ReadyToBoot/Shell. This port buffers logs from Start().
  Start (NULL, NULL);
  return EFI_SUCCESS;
Error:
  if (mExitEvent != NULL) gBS->CloseEvent (mExitEvent);
  gBS->CloseEvent (mPollEvent);
  return Status;
}
