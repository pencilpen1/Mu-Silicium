/** @file
  Single-port MT6768 MUSB PIO EFI_USB2_HC_PROTOCOL.
  EP0 control, EP1 synchronous bulk/interrupt, EP2..5 asynchronous interrupt IN.
  All indexed MMIO is serialized at TPL_NOTIFY; client callbacks run outside
  that critical section at TPL_CALLBACK. Status text has a separate slow timer.
  SPDX-License-Identifier: GPL-2.0-only
**/
#include "MusbHost.h"
#include <Guid/EventGroup.h>
#include <Protocol/DevicePath.h>
#include <Library/BootScreen.h>
#include <Library/PrintLib.h>

STATIC MUSB_HOST mHost;

STATIC UINT64 Now (VOID) { return GetTimeInNanoSecond (GetPerformanceCounter ()); }
STATIC BOOLEAN Valid (EFI_USB2_HC_PROTOCOL *This) { return This == &mHost.Protocol; }

STATIC
VOID
DescribeStatus (CHAR8 *State, UINTN StateSize, CHAR8 *Detail, UINTN DetailSize)
{
  CONST CHAR8 *Text;
  if (EFI_ERROR (mHost.LastError)) {
    Text = "USB HOST: ERROR";
    AsciiSPrint (Detail, DetailSize, "%a: %r",
                 mHost.ErrorPhase ? mHost.ErrorPhase : "Controller", mHost.LastError);
  } else {
    if (mHost.Stopped) Text = "USB HOST: STOPPED";
    else if (!mHost.Partner) Text = "USB HOST: WAITING FOR OTG";
    else if (!mHost.Powered) Text = "USB HOST: CHECKING VBUS";
    else if (!(mHost.Port.PortStatus & USB_PORT_STAT_CONNECTION)) Text = "USB HOST: POWER ON / WAITING DEVICE";
    else if (mHost.Port.PortStatus & USB_PORT_STAT_RESET) Text = "USB HOST: RESETTING DEVICE";
    else if (!mHost.Configured) Text = "USB HOST: ENUMERATING DEVICE";
    else if (mHost.DeviceKind == 1) Text = mHost.InputReceived ?
        "USB HOST: KEYBOARD INPUT RECEIVED" : "USB HOST: KEYBOARD CONFIGURED";
    else if (mHost.DeviceKind == 2) Text = "USB HOST: STORAGE CONFIGURED";
    else if (mHost.DeviceKind == 3) Text = "USB HOST: HUB CONFIGURED";
    else if (mHost.DeviceKind == 4) Text = "USB HOST: HID CONFIGURED";
    else Text = "USB HOST: DEVICE CONFIGURED";
    if (mHost.VendorId || mHost.ProductId) {
      AsciiSPrint (Detail, DetailSize, "%04x:%04x ADDR=%u %a",
                   (UINT32)mHost.VendorId, (UINT32)mHost.ProductId, (UINT32)mHost.DeviceAddress,
                   (mHost.Port.PortStatus & USB_PORT_STAT_HIGH_SPEED) ? "HIGH SPEED" :
                   (mHost.Port.PortStatus & USB_PORT_STAT_LOW_SPEED) ? "LOW SPEED" : "FULL SPEED");
    } else {
      AsciiSPrint (Detail, DetailSize, "%a", mHost.Partner ?
                   "OTG attached; checking USB device" : "Connect OTG + keyboard or USB drive");
    }
  }
  AsciiSPrint (State, StateSize, "%a", Text);
}

STATIC
VOID
EFIAPI
HostScreen (EFI_EVENT Event, VOID *Context)
{
  CHAR8 State[44], Detail[44];
  if (mHost.Stopped) return;
  DescribeStatus (State, sizeof (State), Detail, sizeof (Detail));
  BootScreenText (2244, State, 0xFF002040U);
  BootScreenText (2292, Detail, 0xFF002040U);
}

STATIC
VOID
TransferStatus (EFI_STATUS Status, UINT32 Result)
{
  mHost.LastError = Status;
  mHost.ErrorPhase = (Result & EFI_USB_ERR_STALL) ? "Endpoint STALL" :
                     (Result & EFI_USB_ERR_BABBLE) ? "Packet overflow" :
                     (Result & EFI_USB_ERR_TIMEOUT) ? "USB timeout" : "USB transfer";
}

STATIC
VOID
ObserveControl (UINT8 Address, EFI_USB_DEVICE_REQUEST *Request, VOID *Data, UINTN Length)
{
  UINT8 *Bytes = Data;
  // Observe completed requests only. A parsed descriptor is not itself proof
  // that SET_CONFIGURATION or a class driver succeeded.
  if (Request->RequestType == 0x80 && Request->Request == 6 && Bytes) {
    if ((Request->Value >> 8) == 1 && Length >= 18 && Bytes[0] == 18 && Bytes[1] == 1) {
      if (mHost.DeviceAddress != Address) {
        mHost.Configured = FALSE; mHost.InputReceived = FALSE; mHost.DeviceKind = 0;
      }
      mHost.VendorId = Bytes[8] | ((UINT16)Bytes[9] << 8);
      mHost.ProductId = Bytes[10] | ((UINT16)Bytes[11] << 8);
      mHost.DeviceAddress = Address;
      if (Bytes[4] == 9) mHost.DeviceKind = 3;
    } else if ((Request->Value >> 8) == 2 && Length >= 9 && Bytes[0] == 9 && Bytes[1] == 2) {
      for (UINTN Offset = 0; Offset + 2 <= Length;) {
        UINTN Size = Bytes[Offset];
        if (Size < 2 || Size > Length - Offset) break;
        if (Bytes[Offset + 1] == 4 && Size >= 9 && Bytes[Offset + 3] == 0) {
          UINT8 Class = Bytes[Offset + 5];
          if (Class == 3 && Bytes[Offset + 6] == 1 && Bytes[Offset + 7] == 1)
            mHost.DeviceKind = 1;
          else if (Class == 8 && mHost.DeviceKind != 1) mHost.DeviceKind = 2;
          else if (Class == 9 && mHost.DeviceKind == 0) mHost.DeviceKind = 3;
          else if (Class == 3 && mHost.DeviceKind == 0) mHost.DeviceKind = 4;
        }
        Offset += Size;
      }
    }
  } else if (Request->RequestType == 0 && Request->Request == 5) {
    mHost.DeviceAddress = (UINT8)Request->Value;
    mHost.VendorId = mHost.ProductId = 0;
    mHost.DeviceKind = 0;
    mHost.Configured = mHost.InputReceived = FALSE;
  } else if (Request->RequestType == 0 && Request->Request == 9) {
    mHost.DeviceAddress = Address;
    mHost.Configured = Request->Value != 0;
  }
}

STATIC
VOID
Fifo (UINT8 Ep, VOID *Buffer, UINTN Length, BOOLEAN Read)
{
  UINT8 *Bytes = Buffer;
  for (UINTN I = 0; I < Length; I++) {
    if (Read) Bytes[I] = R8 (REG_FIFO (Ep));
    else W8 (REG_FIFO (Ep), Bytes[I]);
  }
}

STATIC
VOID
Flush (UINT8 Ep, BOOLEAN In)
{
  W8 (REG_INDEX, Ep);
  if (Ep == 0) {
    W16 (REG_TXCSR, 0); // cancel REQPKT before flushing
    W16 (REG_TXCSR, CSR0_FLUSH);
    W16 (REG_TXCSR, CSR0_FLUSH);
    W16 (REG_TXCSR, 0);
  } else if (In) {
    W16 (REG_RXCSR, 0);
    W16 (REG_RXCSR, RX_FLUSH | RX_READY);
    W16 (REG_RXCSR, RX_FLUSH | RX_READY);
    W16 (REG_RXCSR, 0);
  } else {
    W16 (REG_TXCSR, TX_MODE);
    W16 (REG_TXCSR, TX_MODE | TX_FLUSH);
    W16 (REG_TXCSR, TX_MODE | TX_FLUSH);
    W16 (REG_TXCSR, TX_MODE);
  }
  W16 ((In && Ep) ? REG_INTRRX : REG_INTRTX, (UINT16)(1U << Ep));
}

STATIC
VOID
SetToggle (UINT8 Ep, BOOLEAN In, UINT8 Toggle)
{
  UINTN Reg = In ? REG_RXTOG : REG_TXTOG;
  // MTK's upper half is the write-enable mask, not standard Mentor CSR toggle.
  MmioWrite32 (MUSB_BASE + Reg, (1U << (Ep + 16)) | ((UINT32)Toggle << Ep));
  MmioWrite32 (MUSB_BASE + Reg, (UINT32)Toggle << Ep);
}

STATIC
UINT8
GetToggle (UINT8 Ep, BOOLEAN In)
{
  return (UINT8)((MmioRead32 (MUSB_BASE + (In ? REG_RXTOG : REG_TXTOG)) >> Ep) & 1);
}

STATIC
VOID
Target (UINT8 Ep, UINT8 Address, UINT8 Endpoint, UINT8 Speed, UINT8 Protocol,
        UINTN MaxPacket, BOOLEAN In)
{
  UINT8 Type = Speed == EFI_USB_SPEED_HIGH ? 0x40 :
               Speed == EFI_USB_SPEED_LOW ? 0xC0 : 0x80;
  Type |= Protocol | (Endpoint & 0x0F);
  W8 (REG_INDEX, Ep);
  // MediaTek moved these out of the standard 0x80 multipoint register window.
  W16 (REG_TXFUNCADDR + Ep * 8, Address);
  W16 (REG_RXFUNCADDR + Ep * 8, Address);
  W16 (REG_TXHUBADDR + Ep * 8, 0);
  W16 (REG_RXHUBADDR + Ep * 8, 0);
  if (In && Ep) {
    W16 (REG_RXMAXP, (UINT16)MaxPacket);
    W8 (REG_RXTYPE, Type);
    W8 (REG_RXINTERVAL, Protocol == PROTO_INTERRUPT ? 1 : 0);
  } else {
    W16 (REG_TXMAXP, (UINT16)MaxPacket);
    W8 (REG_TXTYPE, Type);
    W8 (REG_TXINTERVAL, Protocol == PROTO_INTERRUPT ? 1 : 0);
  }
}

STATIC
VOID
DropPort (VOID)
{
  if (mHost.Port.PortStatus & USB_PORT_STAT_CONNECTION)
    mHost.Port.PortChangeStatus |= USB_PORT_STAT_C_CONNECTION;
  mHost.Port.PortStatus &= USB_PORT_STAT_POWER;
  mHost.VendorId = mHost.ProductId = 0;
  mHost.DeviceAddress = mHost.DeviceKind = 0;
  mHost.Configured = mHost.InputReceived = FALSE;
  for (UINTN I = 0; I < ASYNC_SLOTS; I++) {
    if (mHost.Async[I].Used) {
      Flush ((UINT8)(I + 2), TRUE);
      mHost.Async[I].Pending = FALSE;
      mHost.Async[I].Failed = TRUE;
    }
  }
}

STATIC
VOID
PhyRole (BOOLEAN Host)
{
  // Same tphy-v1 DTM1 sequence as phy-mtk-tphy.c: forced IDDIG, A/B/VBUS valid,
  // SESSEND; peripheral and host cannot own the PHY concurrently.
  MmioAndThenOr32 (USB_PHY_BASE + 0x6C, ~(UINT32)0x3F,
                   0x3F00 | (Host ? 0x2D : 0x11));
}

STATIC
VOID
PowerOff (VOID)
{
  W8 (REG_POWER, 0);
  W8 (REG_DEVCTL, 0);
  PhyRole (FALSE);
  DropPort ();
  mHost.Port.PortStatus = 0;
  mHost.Powered = FALSE;
  mHost.PowerOffPending = EFI_ERROR (LancelotUsbVbus (FALSE));
  if (mHost.PowerOffPending) {
    mHost.Fault = TRUE;
    mHost.LastError = EFI_DEVICE_ERROR; mHost.ErrorPhase = "VBUS shutdown";
  }
}

STATIC
VOID
BusEvents (VOID)
{
  UINT8 Events = R8 (REG_INTRUSB);
  W8 (REG_INTRUSB, Events); // MTK W1C
  if (!mHost.Powered) return;
  if (Events & USB_VBUS_ERROR) {
    BootLogPrint ("[HOST] VBUS error; power off\n");
    PowerOff ();
    mHost.Fault = TRUE; // recover only after physical detach / port power cycle
    mHost.LastError = EFI_DEVICE_ERROR; mHost.ErrorPhase = "VBUS fault";
    mHost.Port.PortStatus |= USB_PORT_STAT_OVERCURRENT;
    mHost.Port.PortChangeStatus |= USB_PORT_STAT_C_OVERCURRENT;
    return;
  }
  if ((Events & USB_DISCONNECT) ||
      ((Events & USB_BABBLE) && !(mHost.Port.PortStatus & USB_PORT_STAT_RESET))) {
    DropPort ();
    BootLogPrint ("[HOST] disconnect/babble=%02x\n", Events);
    return;
  }
  if ((Events & USB_CONNECT) && !(mHost.Port.PortStatus & USB_PORT_STAT_CONNECTION)) {
    mHost.Port.PortStatus |= USB_PORT_STAT_CONNECTION;
    if (R8 (REG_DEVCTL) & DEVCTL_LSDEV) mHost.Port.PortStatus |= USB_PORT_STAT_LOW_SPEED;
    mHost.Port.PortChangeStatus |= USB_PORT_STAT_C_CONNECTION;
    BootLogPrint ("[HOST] connect DEVCTL=%02x\n", R8 (REG_DEVCTL));
  }
}

STATIC
EFI_STATUS
PowerOn (VOID)
{
  EFI_STATUS Status = LancelotUsbVbus (TRUE);
  if (EFI_ERROR (Status)) return Status;
  mHost.Powered = TRUE;
  mHost.Port.PortStatus = USB_PORT_STAT_POWER;
  MicroSecondDelay (100000); // board VBUS rise, as in do_host_work()
  W8 (REG_DEVCTL, 0);
  PhyRole (FALSE);
  MicroSecondDelay (5000);
  W8 (REG_INTRUSB, 0xFF);
  W8 (REG_DEVCTL, DEVCTL_SESSION);
  PhyRole (TRUE);
  W8 (0x74, (R8 (0x74) | 0x80) & ~0x40); // MTK NOISE_STILL_SOF / BABBLE_CLR_EN
  W8 (REG_POWER, POWER_SOFTCONN | POWER_HSENAB | 1);
  BootLogPrint ("[HOST] VBUS on; session started\n");
  return EFI_SUCCESS;
}

STATIC
VOID
CheckRole (VOID)
{
  BOOLEAN Partner;
  EFI_STATUS Status;
  // An I2C failure does not prove that the boost switch is off. Retry shutdown
  // before accepting any new attach, including after a transient bus error.
  if (mHost.PowerOffPending) {
    mHost.PowerOffPending = EFI_ERROR (LancelotUsbVbus (FALSE));
    if (mHost.PowerOffPending) return;
  }
  Status = LancelotUsbPartnerIsSink (&Partner);
  if (EFI_ERROR (Status) || !Partner) {
    if (mHost.Powered) PowerOff ();
    if (!EFI_ERROR (Status) && !mHost.PowerOffPending) {
      mHost.Fault = FALSE;
      mHost.LastError = EFI_SUCCESS;
    } else {
      mHost.LastError = EFI_ERROR (Status) ? Status : EFI_DEVICE_ERROR;
      mHost.ErrorPhase = "I2C / Type-C";
    }
    mHost.Partner = FALSE;
    return;
  }
  // Require two consecutive sink observations before sourcing VBUS.
  if (mHost.Partner && !mHost.Powered && !mHost.Fault && mHost.WantPower &&
      mHost.State == EfiUsbHcStateOperational) {
    Status = PowerOn ();
    if (EFI_ERROR (Status)) {
      PowerOff ();
      mHost.Fault = TRUE;
      mHost.LastError = Status; mHost.ErrorPhase = "VBUS start";
      BootLogPrint ("[HOST] VBUS start failed: %r\n", Status);
    }
  }
  mHost.Partner = TRUE;
}

STATIC
BOOLEAN
CanTransfer (VOID)
{
  return !mHost.Stopped && mHost.Powered && mHost.State == EfiUsbHcStateOperational &&
    (mHost.Port.PortStatus & (USB_PORT_STAT_CONNECTION | USB_PORT_STAT_ENABLE)) ==
    (USB_PORT_STAT_CONNECTION | USB_PORT_STAT_ENABLE) &&
    !(mHost.Port.PortStatus & (USB_PORT_STAT_RESET | USB_PORT_STAT_SUSPEND));
}

STATIC
EFI_STATUS
CheckTransfer (EFI_USB2_HC_PROTOCOL *This, UINT8 Address, UINT8 Speed,
               UINTN MaxPacket, EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator,
               UINT32 *Result)
{
  if (!Valid (This) || Result == NULL) return EFI_INVALID_PARAMETER;
  *Result = EFI_USB_ERR_SYSTEM;
  if (Address > 127 || Speed > EFI_USB_SPEED_HIGH || MaxPacket == 0)
    return EFI_INVALID_PARAMETER;
  // Split transactions require additional TT recovery/timing support.
  if (Translator != NULL && Translator->TranslatorHubAddress != 0) {
    mHost.LastError = EFI_UNSUPPORTED; mHost.ErrorPhase = "Hub split transfer";
    return EFI_UNSUPPORTED;
  }
  if (mHost.Stopped) return EFI_DEVICE_ERROR;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
WaitPacket (UINT8 Ep, BOOLEAN In, UINT16 Command, UINTN *Budget, UINT32 *Result)
{
  UINT16 Csr, Stall, Error, Nak, Reg;
  Reg = In && Ep ? REG_RXCSR : REG_TXCSR;
  Stall = Ep == 0 ? CSR0_STALL : In ? RX_STALL : TX_STALL;
  Error = Ep == 0 ? CSR0_ERROR : In ? RX_ERROR : TX_ERROR;
  Nak = Ep == 0 ? CSR0_NAK : In ? RX_NAK : TX_NAK;
  for (;;) {
    BusEvents ();
    W8 (REG_INDEX, Ep);
    if (!CanTransfer ()) { *Result = EFI_USB_ERR_SYSTEM; return EFI_DEVICE_ERROR; }
    Csr = R16 (Reg);
    if (Csr & Stall) { *Result = EFI_USB_ERR_STALL; return EFI_DEVICE_ERROR; }
    if (Csr & Error) { *Result = EFI_USB_ERR_CRC; return EFI_DEVICE_ERROR; }
    if (Csr & Nak) {
      // Retry NAK within the caller's single transfer budget, preserving FIFO
      // and hardware toggle. NAK is not a successful zero-length packet.
      W16 (Reg, (Csr & ~Nak) | Command);
    } else if (In) {
      if ((Csr & RX_READY) || ((Ep == 0) && (Command & CSR0_STATUS) &&
          (R16 (REG_INTRTX) & 1) && !(Csr & CSR0_REQPKT))) {
        *Result = EFI_USB_NOERROR;
        return EFI_SUCCESS;
      }
    } else if (!(Csr & (Ep == 0 ? CSR0_TXREADY : TX_READY))) {
      *Result = EFI_USB_NOERROR;
      return EFI_SUCCESS;
    }
    if (*Budget < 10) { *Result = EFI_USB_ERR_TIMEOUT; return EFI_TIMEOUT; }
    MicroSecondDelay (10);
    *Budget -= 10;
  }
}

STATIC
EFI_STATUS
EFIAPI
HostControl (EFI_USB2_HC_PROTOCOL *This, UINT8 Address, UINT8 Speed, UINTN MaxPacket,
             EFI_USB_DEVICE_REQUEST *Request, EFI_USB_DATA_DIRECTION Direction,
             VOID *Data, UINTN *DataLength, UINTN TimeOut,
             EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator, UINT32 *Result)
{
  EFI_STATUS Status = CheckTransfer (This, Address, Speed, MaxPacket, Translator, Result);
  UINTN Length, Done = 0, Count, Budget;
  EFI_TPL Tpl;
  UINT8 *Bytes = Data;
  UINT16 Command;
  if (EFI_ERROR (Status)) return Status;
  if (Request == NULL || (UINT32)Direction > EfiUsbNoData ||
      (MaxPacket != 8 && MaxPacket != 16 && MaxPacket != 32 && MaxPacket != 64) ||
      (Speed == EFI_USB_SPEED_LOW && MaxPacket != 8) ||
      (Speed == EFI_USB_SPEED_HIGH && MaxPacket != 64) || TimeOut > MAX_UINTN / 1000)
    return EFI_INVALID_PARAMETER;
  Length = Request->Length;
  if ((Direction == EfiUsbNoData && Length != 0) ||
      (Length != 0 && (Data == NULL || DataLength == NULL || *DataLength < Length)) ||
      (Length != 0 && ((Request->RequestType & 0x80) != 0) != (Direction == EfiUsbDataIn)))
    return EFI_INVALID_PARAMETER;
  if (DataLength != NULL) *DataLength = 0;
  Budget = TimeOut == 0 ? 5000000 : TimeOut * 1000;
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (!CanTransfer ()) { Status = EFI_DEVICE_ERROR; goto Exit; }
  Flush (0, FALSE);
  Target (0, Address, 0, Speed, 0, MaxPacket, FALSE);
  Fifo (0, Request, sizeof (*Request), FALSE);
  W16 (REG_TXCSR, CSR0_SETUP | CSR0_TXREADY);
  Status = WaitPacket (0, FALSE, CSR0_SETUP | CSR0_TXREADY, &Budget, Result);
  if (EFI_ERROR (Status)) goto Abort;
  while (Done < Length) {
    W16 (REG_INTRTX, 1);
    if (Direction == EfiUsbDataIn) {
      W16 (REG_TXCSR, CSR0_REQPKT);
      Status = WaitPacket (0, TRUE, CSR0_REQPKT, &Budget, Result);
      if (EFI_ERROR (Status)) goto Abort;
      Count = R8 (REG_RXCOUNT);
      if (Count > MaxPacket || Count > Length - Done) {
        *Result = EFI_USB_ERR_BABBLE; Status = EFI_DEVICE_ERROR; goto Abort;
      }
      Fifo (0, Bytes + Done, Count, TRUE);
      W16 (REG_TXCSR, 0);
    } else {
      Count = MIN (MaxPacket, Length - Done);
      Fifo (0, Bytes + Done, Count, FALSE);
      W16 (REG_TXCSR, CSR0_TXREADY);
      Status = WaitPacket (0, FALSE, CSR0_TXREADY, &Budget, Result);
      if (EFI_ERROR (Status)) goto Abort;
    }
    Done += Count;
    if (Count < MaxPacket) break;
  }
  W16 (REG_INTRTX, 1);
  Command = CSR0_STATUS | ((Length == 0 || Direction != EfiUsbDataIn) ? CSR0_REQPKT : CSR0_TXREADY);
  W16 (REG_TXCSR, Command);
  Status = WaitPacket (0, (Command & CSR0_REQPKT) != 0, Command, &Budget, Result);
  if (!EFI_ERROR (Status) && (Command & CSR0_REQPKT) &&
      (R16 (REG_TXCSR) & CSR0_RXREADY) && R8 (REG_RXCOUNT) != 0) {
    *Result = EFI_USB_ERR_BABBLE;
    Status = EFI_DEVICE_ERROR;
  }
Abort:
  if (!EFI_ERROR (Status)) ObserveControl (Address, Request, Data, Done);
  Flush (0, FALSE);
Exit:
  if (DataLength != NULL) *DataLength = Done;
  TransferStatus (Status, *Result);
  gBS->RestoreTPL (Tpl);
  return Status;
}

STATIC
EFI_STATUS
Transfer (EFI_USB2_HC_PROTOCOL *This, UINT8 Address, UINT8 Endpoint, UINT8 Speed,
          UINTN MaxPacket, VOID *Data, UINTN *Length, UINT8 *Toggle, UINTN TimeOut,
          EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator, UINT32 *Result, UINT8 Protocol)
{
  EFI_STATUS Status = CheckTransfer (This, Address, Speed, MaxPacket, Translator, Result);
  EFI_TPL Tpl;
  UINTN Requested, Done = 0, Count, Budget;
  BOOLEAN In = (Endpoint & 0x80) != 0;
  UINT8 *Bytes = Data;
  UINT16 Command = In ? RX_REQPKT : TX_MODE | TX_READY;
  if (EFI_ERROR (Status)) return Status;
  if (Length == NULL || Toggle == NULL || *Toggle > 1 ||
      (Data == NULL && *Length != 0) || (Endpoint & 0x0F) == 0 || (Endpoint & 0x70) ||
      TimeOut > MAX_UINTN / 1000 ||
      (Protocol == PROTO_BULK && Speed == EFI_USB_SPEED_LOW) ||
      MaxPacket > (Speed == EFI_USB_SPEED_HIGH ? 512 : Speed == EFI_USB_SPEED_LOW ? 8 : 64))
    return EFI_INVALID_PARAMETER;
  Requested = *Length;
  *Length = 0;
  Budget = TimeOut == 0 ? 5000000 : TimeOut * 1000;
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (!CanTransfer ()) { Status = EFI_DEVICE_ERROR; goto Exit; }
  Flush (1, In);
  Target (1, Address, Endpoint, Speed, Protocol, MaxPacket, In);
  SetToggle (1, In, *Toggle);
  do {
    W16 (In ? REG_INTRRX : REG_INTRTX, 2);
    if (!In) {
      Count = MIN (Requested - Done, MaxPacket);
      if (Count) Fifo (1, Bytes + Done, Count, FALSE);
    }
    W16 (In ? REG_RXCSR : REG_TXCSR, Command);
    Status = WaitPacket (1, In, Command, &Budget, Result);
    if (EFI_ERROR (Status)) break;
    if (In) {
      Count = R16 (REG_RXCOUNT);
      if (Count > MaxPacket || Count > Requested - Done) {
        *Result = EFI_USB_ERR_BABBLE; Status = EFI_DEVICE_ERROR; break;
      }
      if (Count) Fifo (1, Bytes + Done, Count, TRUE);
      W16 (REG_RXCSR, 0);
    }
    Done += Count;
    if (Count < MaxPacket) break;
  } while (Done < Requested);
  // Hardware toggle is authoritative even when the final packet times out.
  *Toggle = GetToggle (1, In);
  Flush (1, In);
Exit:
  *Length = Done;
  TransferStatus (Status, *Result);
  gBS->RestoreTPL (Tpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
HostBulk (EFI_USB2_HC_PROTOCOL *This, UINT8 Address, UINT8 Endpoint, UINT8 Speed,
          UINTN MaxPacket, UINT8 BufferCount, VOID *Data[EFI_USB_MAX_BULK_BUFFER_NUM],
          UINTN *Length, UINT8 *Toggle, UINTN TimeOut,
          EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator, UINT32 *Result)
{
  if (Result) *Result = EFI_USB_ERR_SYSTEM;
  if (Data == NULL || BufferCount != 1) return EFI_INVALID_PARAMETER;
  return Transfer (This, Address, Endpoint, Speed, MaxPacket, Data[0], Length,
                   Toggle, TimeOut, Translator, Result, PROTO_BULK);
}

STATIC
EFI_STATUS
EFIAPI
HostInterrupt (EFI_USB2_HC_PROTOCOL *This, UINT8 Address, UINT8 Endpoint, UINT8 Speed,
               UINTN MaxPacket, VOID *Data, UINTN *Length, UINT8 *Toggle, UINTN TimeOut,
               EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator, UINT32 *Result)
{
  return Transfer (This, Address, Endpoint, Speed, MaxPacket, Data, Length,
                   Toggle, TimeOut, Translator, Result, PROTO_INTERRUPT);
}

STATIC
EFI_STATUS
EFIAPI
HostAsync (EFI_USB2_HC_PROTOCOL *This, UINT8 Address, UINT8 Endpoint, UINT8 Speed,
           UINTN MaxPacket, BOOLEAN New, UINT8 *Toggle, UINTN Interval, UINTN Length,
           EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator,
           EFI_ASYNC_USB_TRANSFER_CALLBACK Callback, VOID *Context)
{
  EFI_TPL Tpl;
  MUSB_ASYNC *Slot = NULL;
  UINTN Index = 0;
  UINT32 Result;
  EFI_STATUS Status;
  if (!Valid (This) || Toggle == NULL || Address > 127 || !(Endpoint & 0x80) ||
      !(Endpoint & 0x0F) || (Endpoint & 0x70)) return EFI_INVALID_PARAMETER;
  if (New) {
    Status = CheckTransfer (This, Address, Speed, MaxPacket, Translator, &Result);
    if (EFI_ERROR (Status)) return Status;
    if (*Toggle > 1 || !Callback || Interval < 1 || Interval > 255 || !Length ||
        Length > MaxPacket || MaxPacket > ASYNC_PACKET ||
        (Speed == EFI_USB_SPEED_LOW && MaxPacket > 8)) return EFI_INVALID_PARAMETER;
  }
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  for (UINTN I = 0; I < ASYNC_SLOTS; I++) {
    if (mHost.Async[I].Used && mHost.Async[I].Address == Address && mHost.Async[I].Endpoint == Endpoint) {
      if (New) { gBS->RestoreTPL (Tpl); return EFI_INVALID_PARAMETER; }
      Slot = &mHost.Async[I]; Index = I; break;
    }
  }
  if (!New) {
    if (Slot == NULL) { gBS->RestoreTPL (Tpl); return EFI_NOT_FOUND; }
    if (!mHost.Stopped) {
      if (Slot->Pending) Slot->Toggle = GetToggle ((UINT8)(Index + 2), TRUE);
      Flush ((UINT8)(Index + 2), TRUE);
    }
    *Toggle = Slot->Toggle;
    ZeroMem (Slot, sizeof (*Slot));
    gBS->RestoreTPL (Tpl);
    return EFI_SUCCESS;
  }
  if (!CanTransfer ()) { gBS->RestoreTPL (Tpl); return EFI_DEVICE_ERROR; }
  for (UINTN I = 0; I < ASYNC_SLOTS; I++) {
    if (!mHost.Async[I].Used) { Slot = &mHost.Async[I]; break; }
  }
  if (Slot == NULL) { gBS->RestoreTPL (Tpl); return EFI_OUT_OF_RESOURCES; }
  ZeroMem (Slot, sizeof (*Slot));
  Slot->Used = TRUE; Slot->Address = Address; Slot->Endpoint = Endpoint;
  Slot->Speed = Speed; Slot->Toggle = *Toggle; Slot->MaxPacket = MaxPacket;
  Slot->Length = Length; Slot->IntervalNs = Interval * 1000000ULL;
  Slot->Callback = Callback; Slot->Context = Context; Slot->Due = Now ();
  gBS->RestoreTPL (Tpl);
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
HostTick (EFI_EVENT Event, VOID *Context)
{
  EFI_TPL Tpl;
  UINT64 Time;
  if (mHost.Stopped) return;
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  Time = Now ();
  if (Time >= mHost.RoleDue) {
    CheckRole ();
    mHost.RoleDue = Now () + 50000000ULL;
  }
  BusEvents ();
  gBS->RestoreTPL (Tpl);
  for (UINTN I = 0; I < ASYNC_SLOTS; I++) {
    MUSB_ASYNC *Slot = &mHost.Async[I];
    UINT8 Ep = (UINT8)(I + 2), Buffer[ASYNC_PACKET];
    UINTN Count = 0;
    UINT32 Result = EFI_USB_NOERROR;
    EFI_ASYNC_USB_TRANSFER_CALLBACK Callback = NULL;
    VOID *CallbackContext = NULL;
    Tpl = gBS->RaiseTPL (TPL_NOTIFY);
    if (mHost.Stopped) { gBS->RestoreTPL (Tpl); return; }
    if (Slot->Used && !Slot->Failed && CanTransfer ()) {
      W8 (REG_INDEX, Ep);
      if (Slot->Pending) {
        UINT16 Csr = R16 (REG_RXCSR);
        if (Csr & (RX_STALL | RX_ERROR | RX_NAK | RX_READY)) {
          if (Csr & RX_STALL) Result = EFI_USB_ERR_STALL;
          else if (Csr & RX_ERROR) Result = EFI_USB_ERR_CRC;
          else if (!(Csr & RX_NAK)) {
            Count = R16 (REG_RXCOUNT);
            if (Count > Slot->Length || Count > Slot->MaxPacket) {
              Result = EFI_USB_ERR_BABBLE; Count = 0;
            } else {
              Fifo (Ep, Buffer, Count, TRUE);
            }
          }
          Slot->Toggle = GetToggle (Ep, TRUE);
          Flush (Ep, TRUE);
          Slot->Pending = FALSE;
          Slot->Due = Now () + Slot->IntervalNs;
          if (!(Csr & RX_NAK) || Result != EFI_USB_NOERROR) {
            Callback = Slot->Callback; CallbackContext = Slot->Context;
            if (Result != EFI_USB_NOERROR) TransferStatus (EFI_DEVICE_ERROR, Result);
            else if (Count && Slot->Address == mHost.DeviceAddress && mHost.DeviceKind == 1)
              mHost.InputReceived = TRUE;
          }
        }
      } else if (Now () >= Slot->Due) {
        Flush (Ep, TRUE);
        Target (Ep, Slot->Address, Slot->Endpoint, Slot->Speed,
                PROTO_INTERRUPT, Slot->MaxPacket, TRUE);
        SetToggle (Ep, TRUE, Slot->Toggle);
        W16 (REG_RXCSR, RX_REQPKT);
        Slot->Pending = TRUE;
      }
    }
    gBS->RestoreTPL (Tpl);
    // Callback may cancel itself or create a new pipe. Do not access Slot
    // afterwards, and pass a private snapshot rather than live FIFO storage.
    if (Callback) Callback (Buffer, Count, CallbackContext, Result);
  }
}

STATIC
EFI_STATUS
EFIAPI
HostCapability (EFI_USB2_HC_PROTOCOL *This, UINT8 *Speed, UINT8 *Ports, UINT8 *Address64)
{
  if (!Valid (This) || !Speed || !Ports || !Address64) return EFI_INVALID_PARAMETER;
  *Speed = EFI_USB_SPEED_HIGH; *Ports = 1; *Address64 = FALSE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
HostGetState (EFI_USB2_HC_PROTOCOL *This, EFI_USB_HC_STATE *State)
{
  if (!Valid (This) || State == NULL) return EFI_INVALID_PARAMETER;
  *State = mHost.Stopped ? EfiUsbHcStateHalt : mHost.State;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
HostSetState (EFI_USB2_HC_PROTOCOL *This, EFI_USB_HC_STATE State)
{
  EFI_TPL Tpl;
  if (!Valid (This) || (UINT32)State >= EfiUsbHcStateMaximum) return EFI_INVALID_PARAMETER;
  if (mHost.Stopped) return EFI_DEVICE_ERROR;
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (State == EfiUsbHcStateHalt) PowerOff ();
  else if (State == EfiUsbHcStateSuspend) {
    if (mHost.Powered) W8 (REG_POWER, R8 (REG_POWER) | POWER_SUSPEND);
  } else if (mHost.State == EfiUsbHcStateSuspend && mHost.Powered) {
    W8 (REG_POWER, (R8 (REG_POWER) & ~POWER_SUSPEND) | POWER_RESUME);
    MicroSecondDelay (20000);
    W8 (REG_POWER, R8 (REG_POWER) & ~POWER_RESUME);
  }
  mHost.State = State;
  gBS->RestoreTPL (Tpl);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
HostReset (EFI_USB2_HC_PROTOCOL *This, UINT16 Attributes)
{
  EFI_TPL Tpl;
  if (!Valid (This)) return EFI_INVALID_PARAMETER;
  if (Attributes != EFI_USB_HC_RESET_GLOBAL && Attributes != EFI_USB_HC_RESET_HOST_CONTROLLER)
    return EFI_UNSUPPORTED;
  if (mHost.Stopped) return EFI_DEVICE_ERROR;
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  PowerOff ();
  Flush (0, FALSE); Flush (1, FALSE); Flush (1, TRUE);
  ZeroMem (mHost.Async, sizeof (mHost.Async));
  mHost.State = EfiUsbHcStateHalt;
  mHost.Fault = FALSE;
  gBS->RestoreTPL (Tpl);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
HostPortStatus (EFI_USB2_HC_PROTOCOL *This, UINT8 Port, EFI_USB_PORT_STATUS *Status)
{
  EFI_TPL Tpl;
  if (!Valid (This) || Port != 0 || Status == NULL) return EFI_INVALID_PARAMETER;
  if (mHost.Stopped) return EFI_DEVICE_ERROR;
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  BusEvents ();
  *Status = mHost.Port;
  gBS->RestoreTPL (Tpl);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
HostSetPort (EFI_USB2_HC_PROTOCOL *This, UINT8 Port, EFI_USB_PORT_FEATURE Feature)
{
  EFI_TPL Tpl;
  EFI_STATUS Status = EFI_SUCCESS;
  if (!Valid (This) || Port != 0) return EFI_INVALID_PARAMETER;
  if (mHost.Stopped) return EFI_DEVICE_ERROR;
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  switch (Feature) {
    case EfiUsbPortPower:
      mHost.WantPower = TRUE; mHost.Fault = FALSE;
      CheckRole ();
      break;
    case EfiUsbPortReset:
      if (!mHost.Powered || !(mHost.Port.PortStatus & USB_PORT_STAT_CONNECTION)) {
        Status = EFI_DEVICE_ERROR; break;
      }
      Flush (0, FALSE); Flush (1, FALSE); Flush (1, TRUE);
      for (UINTN I = 0; I < ASYNC_SLOTS; I++) {
        if (mHost.Async[I].Used) {
          Flush ((UINT8)(I + 2), TRUE);
          mHost.Async[I].Pending = FALSE; mHost.Async[I].Failed = TRUE;
        }
      }
      mHost.Port.PortStatus &= ~(USB_PORT_STAT_ENABLE | USB_PORT_STAT_LOW_SPEED | USB_PORT_STAT_HIGH_SPEED);
      mHost.Port.PortStatus |= USB_PORT_STAT_RESET;
      mHost.ResetStarted = Now ();
      W8 (REG_POWER, (R8 (REG_POWER) & ~(POWER_SUSPEND | POWER_RESUME)) | POWER_RESET);
      break;
    case EfiUsbPortEnable:
      Status = (mHost.Port.PortStatus & USB_PORT_STAT_ENABLE) ? EFI_SUCCESS : EFI_UNSUPPORTED;
      break;
    case EfiUsbPortSuspend:
      if (!CanTransfer ()) { Status = EFI_DEVICE_ERROR; break; }
      W8 (REG_POWER, R8 (REG_POWER) | POWER_SUSPEND);
      mHost.Port.PortStatus |= USB_PORT_STAT_SUSPEND;
      break;
    default: Status = EFI_INVALID_PARAMETER;
  }
  gBS->RestoreTPL (Tpl);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
HostClearPort (EFI_USB2_HC_PROTOCOL *This, UINT8 Port, EFI_USB_PORT_FEATURE Feature)
{
  EFI_TPL Tpl;
  EFI_STATUS Status = EFI_SUCCESS;
  if (!Valid (This) || Port != 0) return EFI_INVALID_PARAMETER;
  if (mHost.Stopped) return EFI_DEVICE_ERROR;
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);
  switch (Feature) {
    case EfiUsbPortPower: mHost.WantPower = FALSE; PowerOff (); break;
    case EfiUsbPortEnable:
      mHost.Port.PortStatus &= ~USB_PORT_STAT_ENABLE;
      mHost.Port.PortChangeStatus |= USB_PORT_STAT_C_ENABLE;
      Flush (0, FALSE); Flush (1, FALSE); Flush (1, TRUE);
      for (UINTN I = 0; I < ASYNC_SLOTS; I++) {
        if (mHost.Async[I].Used) {
          Flush ((UINT8)(I + 2), TRUE);
          mHost.Async[I].Pending = FALSE;
          mHost.Async[I].Failed = TRUE;
        }
      }
      break;
    case EfiUsbPortReset:
      if (!(mHost.Port.PortStatus & USB_PORT_STAT_RESET)) break;
      {
        UINT64 Elapsed = Now () - mHost.ResetStarted;
        if (Elapsed < 50000000ULL)
          MicroSecondDelay ((UINTN)((50000000ULL - Elapsed) / 1000 + 1));
      }
      W8 (REG_POWER, R8 (REG_POWER) & ~POWER_RESET);
      MicroSecondDelay (20000);
      BusEvents ();
      mHost.Port.PortStatus &= ~USB_PORT_STAT_RESET;
      if (mHost.Port.PortStatus & USB_PORT_STAT_CONNECTION) {
        mHost.Port.PortStatus |= USB_PORT_STAT_ENABLE;
        if (R8 (REG_POWER) & POWER_HSMODE) mHost.Port.PortStatus |= USB_PORT_STAT_HIGH_SPEED;
        else if (R8 (REG_DEVCTL) & DEVCTL_LSDEV) mHost.Port.PortStatus |= USB_PORT_STAT_LOW_SPEED;
        mHost.Port.PortChangeStatus |= USB_PORT_STAT_C_RESET | USB_PORT_STAT_C_ENABLE;
        BootLogPrint ("[HOST] port reset status=%04x\n", mHost.Port.PortStatus);
      } else Status = EFI_DEVICE_ERROR;
      break;
    case EfiUsbPortSuspend:
      if (mHost.Port.PortStatus & USB_PORT_STAT_SUSPEND) {
        W8 (REG_POWER, (R8 (REG_POWER) & ~POWER_SUSPEND) | POWER_RESUME);
        MicroSecondDelay (20000);
        W8 (REG_POWER, R8 (REG_POWER) & ~POWER_RESUME);
        mHost.Port.PortStatus &= ~USB_PORT_STAT_SUSPEND;
        mHost.Port.PortChangeStatus |= USB_PORT_STAT_C_SUSPEND;
      }
      break;
    case EfiUsbPortConnectChange: mHost.Port.PortChangeStatus &= ~USB_PORT_STAT_C_CONNECTION; break;
    case EfiUsbPortEnableChange: mHost.Port.PortChangeStatus &= ~USB_PORT_STAT_C_ENABLE; break;
    case EfiUsbPortSuspendChange: mHost.Port.PortChangeStatus &= ~USB_PORT_STAT_C_SUSPEND; break;
    case EfiUsbPortOverCurrentChange: mHost.Port.PortChangeStatus &= ~USB_PORT_STAT_C_OVERCURRENT; break;
    case EfiUsbPortResetChange: mHost.Port.PortChangeStatus &= ~USB_PORT_STAT_C_RESET; break;
    default: Status = EFI_INVALID_PARAMETER;
  }
  gBS->RestoreTPL (Tpl);
  return Status;
}

STATIC EFI_STATUS EFIAPI
HostIso (EFI_USB2_HC_PROTOCOL *This, UINT8 Address, UINT8 Endpoint, UINT8 Speed,
         UINTN MaxPacket, UINT8 BufferCount, VOID *Data[EFI_USB_MAX_ISO_BUFFER_NUM],
         UINTN Length, EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator, UINT32 *Result)
{
  if (Result) *Result = EFI_USB_ERR_SYSTEM;
  return EFI_UNSUPPORTED;
}

STATIC EFI_STATUS EFIAPI
HostAsyncIso (EFI_USB2_HC_PROTOCOL *This, UINT8 Address, UINT8 Endpoint, UINT8 Speed,
              UINTN MaxPacket, UINT8 BufferCount, VOID *Data[EFI_USB_MAX_ISO_BUFFER_NUM],
              UINTN Length, EFI_USB2_HC_TRANSACTION_TRANSLATOR *Translator,
              EFI_ASYNC_USB_TRANSFER_CALLBACK Callback, VOID *Context)
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
HardwareInit (VOID)
{
  if (!(MmioRead32 (0x1000C238) & BIT0)) return EFI_NOT_READY;
  MmioWrite32 (0x10000098, BIT23 | BIT16);
  MmioWrite32 (0x10000094, BIT16);
  MmioWrite32 (0x10000004, BIT22);
  MmioWrite32 (0x10001084, BIT8);
  MicroSecondDelay (50);
  if ((MmioRead32 (0x10001090) & BIT8) || (MmioRead32 (0x10000090) & BIT23))
    return EFI_DEVICE_ERROR;
  MmioAnd32 (USB_PHY_BASE + 0x68, ~(UINT32)(BIT26 | BIT18 | BIT30 | BIT31));
  MmioAnd32 (USB_PHY_BASE + 0x6C, ~(UINT32)BIT16);
  MmioOr32 (USB_PHY_BASE, BIT5);
  MmioAnd32 (USB_PHY_BASE + 0x14, ~(UINT32)BIT11);
  MmioAndThenOr32 (USB_PHY_BASE + 0x20, ~(UINT32)(BIT9 | BIT8 | BIT16 | BIT17), BIT18);
  MmioAndThenOr32 (USB_PHY_BASE + 0x18, ~(UINT32)BIT23, BIT20);
  MmioAnd32 (USB_PHY_BASE + 0x68, ~(UINT32)(BIT23 | BIT21 | BIT20 | BIT19 | BIT18 |
                        BIT17 | (15U << 10) | BIT7 | BIT6 | BIT5 | BIT4 | BIT2));
  PhyRole (FALSE);
  MicroSecondDelay (800);
  W8 (REG_POWER, 0);
  W8 (REG_DEVCTL, 0);
  MicroSecondDelay (10000);
  MmioWrite32 (MUSB_BASE + 0xA4, 0);
  W16 (REG_INTRTXE, 0); W16 (REG_INTRRXE, 0); W8 (REG_INTRUSBE, 0);
  for (UINTN I = 0; I < 8; I++) MmioWrite32 (MUSB_BASE + 0x204 + I * 16, 0);
  MmioWrite32 (MUSB_BASE + 0xB00, 0);
  W8 (0x0F, 0);
  if (R16 (REG_HWVERS) == 0 || R16 (REG_HWVERS) == 0xFFFF) return EFI_DEVICE_ERROR;
  Flush (0, FALSE);
  W8 (REG_INDEX, 1);
  if (R8 (REG_INDEX) != 1) return EFI_DEVICE_ERROR;
  // EP0 [0,64), EP1 TX [64,576), RX [576,1088), async RX 4 * 64 bytes.
  W8 (REG_TXFIFOSZ, 6); W16 (REG_TXFIFOADD, 64 / 8);
  W8 (REG_RXFIFOSZ, 6); W16 (REG_RXFIFOADD, 576 / 8);
  Flush (1, FALSE); Flush (1, TRUE);
  for (UINTN I = 0; I < ASYNC_SLOTS; I++) {
    W8 (REG_INDEX, (UINT8)(I + 2));
    W8 (REG_RXFIFOSZ, 3); W16 (REG_RXFIFOADD, (UINT16)((1088 + I * ASYNC_PACKET) / 8));
    Flush ((UINT8)(I + 2), TRUE);
  }
  W8 (REG_INTRUSB, 0xFF); W16 (REG_INTRTX, 0xFFFF); W16 (REG_INTRRX, 0xFFFF);
  W16 (REG_INTRTXE, 3); W16 (REG_INTRRXE, (1U << (ASYNC_SLOTS + 2)) - 2);
  W8 (REG_INTRUSBE, USB_CONNECT | USB_DISCONNECT | USB_VBUS_ERROR | USB_BABBLE);
  W8 (REG_INDEX, 0);
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
HostExit (EFI_EVENT Event, VOID *Context)
{
  if (mHost.Stopped) return;
  mHost.Stopped = TRUE;
  if (mHost.MacReady) {
    PowerOff ();
    W16 (REG_INTRTXE, 0); W16 (REG_INTRRXE, 0); W8 (REG_INTRUSBE, 0);
  }
  LancelotUsbBoardExit ();
  BootLogPrint ("[HOST] stopped at ExitBootServices\n");
}

STATIC struct {
  VENDOR_DEVICE_PATH Vendor;
  EFI_DEVICE_PATH_PROTOCOL End;
} mDevicePath = {
  {{HARDWARE_DEVICE_PATH, HW_VENDOR_DP, {sizeof (VENDOR_DEVICE_PATH), 0}},
   {0xC663BDBA, 0x795F, 0x4BB0, {0x9F, 0x34, 0x7F, 0x66, 0xD9, 0x3C, 0x3C, 0x1D}}},
  {END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE, {sizeof (EFI_DEVICE_PATH_PROTOCOL), 0}}
};

EFI_STATUS
EFIAPI
MusbHostEntry (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_HANDLE Handle = NULL;
  EFI_STATUS Status;
  BootLogInitialize (GetPerformanceCounter ());
  BootLogPrint ("[HOST] lancelot MUSB PIO host 0.1\n");
  mHost.Protocol = (EFI_USB2_HC_PROTOCOL) {
    HostCapability, HostReset, HostGetState, HostSetState, HostControl, HostBulk,
    HostAsync, HostInterrupt, HostIso, HostAsyncIso, HostPortStatus, HostSetPort,
    HostClearPort, 2, 0
  };
  // Keep a small status-only resident driver if board bring-up fails. No HC
  // protocol is published in that case, and the screen does not access MMIO.
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, HostScreen, NULL, &mHost.ScreenEvent);
  if (EFI_ERROR (Status)) return Status;
  Status = gBS->CreateEventEx (EVT_NOTIFY_SIGNAL, TPL_NOTIFY, HostExit, NULL,
                             &gEfiEventExitBootServicesGuid, &mHost.ExitEvent);
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (mHost.ScreenEvent);
    return Status;
  }
  Status = gBS->SetTimer (mHost.ScreenEvent, TimerPeriodic, 10000000); // 1 second
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (mHost.ScreenEvent); gBS->CloseEvent (mHost.ExitEvent);
    return Status;
  }
  mHost.ErrorPhase = "I2C / Type-C init";
  Status = LancelotUsbBoardInit ();
  if (EFI_ERROR (Status)) goto BoardError;
  mHost.ErrorPhase = "USB clock / PHY";
  Status = HardwareInit ();
  if (EFI_ERROR (Status)) goto BoardError;
  mHost.MacReady = TRUE;
  mHost.State = EfiUsbHcStateOperational;
  mHost.WantPower = TRUE;
  Status = gBS->CreateEvent (EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, HostTick, NULL, &mHost.PollEvent);
  if (EFI_ERROR (Status)) goto Error;
  Status = gBS->SetTimer (mHost.PollEvent, TimerPeriodic, 10000);
  if (EFI_ERROR (Status)) goto Error;
  Status = gBS->InstallMultipleProtocolInterfaces (&Handle,
             &gEfiDevicePathProtocolGuid, &mDevicePath,
             &gEfiUsb2HcProtocolGuid, &mHost.Protocol, NULL);
  if (!EFI_ERROR (Status)) return EFI_SUCCESS;
Error:
  PowerOff ();
  mHost.State = EfiUsbHcStateHalt;
  mHost.ErrorPhase = "UEFI host services";
  if (mHost.PollEvent) gBS->CloseEvent (mHost.PollEvent);
BoardError:
  LancelotUsbBoardExit ();
  mHost.LastError = Status;
  BootLogPrint ("[HOST] initialization failed: %r\n", Status);
  return EFI_SUCCESS; // status display only; no controller protocol installed
}
