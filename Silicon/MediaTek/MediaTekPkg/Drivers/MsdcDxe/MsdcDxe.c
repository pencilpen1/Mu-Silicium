#include "MsdcDxe.h"

MSDC_PRIVATE_DATA gMSDCPrivateDataTemplate = {
  MSDC_PRIVATE_SIGNATURE, // Signature
  NULL, // ControllerHandle
  {
    sizeof (UINT32),
    MsdcPassThru, // PassThru
    MsdcGetNextSlot, // GetNextSlot
    MsdcBuildDevicePath, // BuildDevicePath
    MsdcGetSlotNumber, // GetSlotNumber
    MsdcResetDevice  // ResetDevice
  }, // PassThru
  0, // Index
  0, // MsdcMmioReg
  0, // TopMmioReg
  {0}, // HostData
  {0}  // SdInfo
};

typedef struct {
  VENDOR_DEVICE_PATH Mmc;
  EFI_DEVICE_PATH    End;
} MSDC_DEVICE_PATH;

MSDC_DEVICE_PATH gMSDCDevicePathTemplate = {
  {  // Mmc (VENDOR_DEVICE_PATH)
    {  // Header
      HARDWARE_DEVICE_PATH,      // Type
      HW_VENDOR_DP,              // SubType
      {                          // Length (UINT8 array style)
        (UINT8)(sizeof(VENDOR_DEVICE_PATH)),
        (UINT8)((sizeof(VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    // Vendor GUID
    { 0xb615f1f5, 0x5088, 0x43cd, { 0x80, 0x9c, 0xa1, 0x6e, 0x52, 0x48, 0x7d, 0x00 } }
  },
  {  // End (EFI_DEVICE_PATH)
    END_DEVICE_PATH_TYPE,        // Type
    END_ENTIRE_DEVICE_PATH_SUBTYPE,  // SubType
    {                            // Length (UINT8 array style)
      sizeof(EFI_DEVICE_PATH_PROTOCOL),
      0
    }
  }
};

EMMC_DEVICE_PATH gEMMCDevicePathTemplate = {
  {
    MESSAGING_DEVICE_PATH,
    MSG_EMMC_DP,
    {
      (UINT8)(sizeof (EMMC_DEVICE_PATH)),
      (UINT8)((sizeof (EMMC_DEVICE_PATH)) >> 8)
    }
  },
  0
};

SD_DEVICE_PATH gSDDevicePathTemplate = {
  {
    MESSAGING_DEVICE_PATH,
    MSG_SD_DP,
    {
      (UINT8)(sizeof (SD_DEVICE_PATH)),
      (UINT8)((sizeof (SD_DEVICE_PATH)) >> 8)
    }
  },
  0
};

EFI_STATUS
MsdcPassThru (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *This,
  UINT8 Slot,
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *Packet,
  EFI_EVENT Event)
{
  MSDC_PRIVATE_DATA *Private;
  EFI_STATUS Status;

  if ((This == NULL) || (Packet == NULL) || (Slot != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Packet->SdMmcCmdBlk == NULL) || (Packet->SdMmcStatusBlk == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Packet->OutDataBuffer == NULL) && (Packet->OutTransferLength != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((Packet->InDataBuffer == NULL) && (Packet->InTransferLength != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  Private = MSDC_PRIVATE_FROM_THIS (This);


  Status = MsdcSendCmd (Private, Packet);
  Packet->TransactionStatus = Status;
  // The PIO controller completes synchronously, including requests supplied
  // with an Event by BlockIo2. Do not leave the upper-layer queue pending.
  // An immediate failure is returned without signalling: callers free that
  // failed request themselves. Signal may free Packet; never touch it after.
  if (!EFI_ERROR (Status) && Event != NULL) gBS->SignalEvent (Event);
  return Status;
}

EFI_STATUS
MsdcGetNextSlot (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *This,
  UINT8 *Slot)
{
  if (This == NULL || Slot == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (*Slot == 0xFF) {
    *Slot = 0;
    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

EFI_STATUS
MsdcBuildDevicePath (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *This,
  UINT8 Slot,
  EFI_DEVICE_PATH_PROTOCOL **DevicePath)
{
  MSDC_PRIVATE_DATA *Private;
  SD_DEVICE_PATH    *SdNode;
  EMMC_DEVICE_PATH  *EmmcNode;

  if (This == NULL || DevicePath == NULL || Slot != 0) {
    return EFI_INVALID_PARAMETER;
  }

  Private = MSDC_PRIVATE_FROM_THIS (This);

  if (Private->SdInfo.CardType == EmmcCard) {
    EmmcNode = AllocateCopyPool (sizeof (EMMC_DEVICE_PATH), &gEMMCDevicePathTemplate);
    if (EmmcNode == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    *DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)EmmcNode;
  } else if (Private->SdInfo.CardType == SdCard) {
    SdNode = AllocateCopyPool (sizeof (SD_DEVICE_PATH), &gSDDevicePathTemplate);
    if (SdNode == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    *DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)SdNode;
  } else {
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
MsdcGetSlotNumber (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *This,
  EFI_DEVICE_PATH_PROTOCOL *DevicePath,
  UINT8 *Slot)
{
  if ((This == NULL) || (DevicePath == NULL) || (Slot == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((DevicePath->Type != MESSAGING_DEVICE_PATH) ||
      ((DevicePath->SubType != MSG_SD_DP) &&
       (DevicePath->SubType != MSG_EMMC_DP)) ||
      (DevicePathNodeLength (DevicePath) != sizeof (SD_DEVICE_PATH)) ||
      (DevicePathNodeLength (DevicePath) != sizeof (EMMC_DEVICE_PATH)))
  {
    return EFI_UNSUPPORTED;
  }

  *Slot = 0;

  return EFI_SUCCESS;
}

EFI_STATUS
MsdcResetDevice (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *This,
  UINT8 Slot)
{
  return EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
MsdcWaitRegister (
  MSDC_PRIVATE_DATA *Private,
  UINT32 Offset,
  UINT32 Mask,
  UINT32 Expected)
{
  UINT32 Reg = 0;
  for (UINT32 Attempt = 0; Attempt < 10000; Attempt++) {
    MsdcRead (Private, Offset, &Reg);
    if ((Reg & Mask) == Expected) return EFI_SUCCESS;
    MicroSecondDelay (100);
  }
  DEBUG ((DEBUG_ERROR, "[WOA][MSDC%u] timeout offset=0x%x mask=0x%x value=0x%x expected=0x%x\n",
          Private->Index, Offset, Mask, Reg, Expected));
  return EFI_TIMEOUT;
}

EFI_STATUS
MsdcReset (MSDC_PRIVATE_DATA *Private)
{
  MsdcSetBits (Private, MSDC_CFG, MSDC_CFG_RST);
  return MsdcWaitRegister (Private, MSDC_CFG, MSDC_CFG_RST, 0);
}

EFI_STATUS
MsdcClearFifo (MSDC_PRIVATE_DATA *Private)
{
  MsdcSetBits (Private, MSDC_FIFOCS, MSDC_FIFOCS_CLR);
  return MsdcWaitRegister (Private, MSDC_FIFOCS, MSDC_FIFOCS_CLR, 0);
}

VOID
MsdcClearInterrupts (
  MSDC_PRIVATE_DATA* Private)
{
  UINT32 Reg;
  MsdcRead (Private, MSDC_INT, &Reg);
  MsdcWrite (Private, MSDC_INT, Reg);
}

VOID
MsdcSetTimeout (
  MSDC_PRIVATE_DATA* Private)
{
  UINT32 CfgReg, Timeout, ClkNs;

  ClkNs = 1000000000 / Private->HostData.Sclk;
  Timeout = (Private->HostData.TimeoutNs + ClkNs - 1) / ClkNs + Private->HostData.TimeoutClks;
  Timeout = (Timeout + (1 << SCLK_CYCLES_SHIFT) - 1) >> SCLK_CYCLES_SHIFT;
  Timeout = Timeout > 1 ? Timeout - 1 : 0;
  Timeout = Timeout > 255 ? 255 : Timeout;

  MsdcRead (Private, SDC_CFG, &CfgReg);
  // Clear BIT24:BIT31
  CfgReg &= ~(0xff << 24);

  CfgReg |= Timeout << 24;
  MsdcWrite (Private, SDC_CFG, CfgReg);
}

VOID
MsdcSetBusWidth (
  MSDC_PRIVATE_DATA* Private,
  UINT32 Width)
{
  UINT32 CfgReg, BusWidth;

  switch (Width) {
    case 1:
      BusWidth = MSDC_BUS_WIDTH_1;
      break;
    case 4:
      BusWidth = MSDC_BUS_WIDTH_4;
      break;
    case 8:
      BusWidth = MSDC_BUS_WIDTH_8;
      break;
    default:
      BusWidth = MSDC_BUS_WIDTH_1;
      break;
  }

  MsdcRead (Private, SDC_CFG, &CfgReg);
  // Clear BIT16:BIT17
  CfgReg &= ~(3 << SDC_CFG_BUS_WIDTH_SHIFT);

  CfgReg |= BusWidth << SDC_CFG_BUS_WIDTH_SHIFT;
  MsdcWrite (Private, SDC_CFG, CfgReg);
}

EFI_STATUS
MsdcSetMclk (
  MSDC_PRIVATE_DATA* Private,
  UINT32 Hz)
{
  UINTN SourceClock;
  UINT32 Div, Mode;
  GetSourceClockRate (Private->Index, &SourceClock);
  if (Hz == 0 || SourceClock == 0) return EFI_DEVICE_ERROR;

  if (Hz >= SourceClock) {
    // Ignore divisor
    Div = 0;
    Mode = MSDC_MCLK_NO_DIV;
    Private->HostData.Sclk = SourceClock;
  } else {
    // Divisor mode
    Mode = MSDC_MCLK_DIV;
    if (Hz >= (SourceClock >> 1)) {
      Div = 0; /* Will divide source clock 1/2 */
      Private->HostData.Sclk = SourceClock >> 1;
    } else {
      Div = (SourceClock + ((Hz << 2) - 1)) / (Hz << 2);
      Private->HostData.Sclk = (SourceClock >> 2) / Div;
    }
  }

  DEBUG ((DEBUG_ERROR, "Hz: %d, Mode: %d, Div: %d, Sclk: %d\n", Hz, Mode, Div, Private->HostData.Sclk));

  UINT32 CfgReg;
  MsdcRead (Private, MSDC_CFG, &CfgReg);
  // Clear CCKMD (BIT20:21)
  CfgReg &= ~(3 << 20);
  // Clear CCKDIV (BIT8:19)
  CfgReg &= ~(0xfff << 8);
  // Clear hs400 div
  CfgReg &= ~(MSDC_CFG_HS400CKMD);
  // Set values
  CfgReg |= (Mode << 20);
  CfgReg |= (Div << 8);

  // Disable source clock before changing mclk
  MsdcClrBits (Private, MSDC_CFG, MSDC_CFG_CCKPD);
  SourceClockControl (Private->Index, FALSE);
  // Change configuration
  MsdcWrite (Private, MSDC_CFG, CfgReg);
  // Enable source clock
  SourceClockControl (Private->Index, TRUE);
  // A failed clock must not hang firmware indefinitely.
  EFI_STATUS ClockStatus = MsdcWaitRegister (Private, MSDC_CFG, MSDC_CFG_CCKSB, MSDC_CFG_CCKSB);
  if (EFI_ERROR (ClockStatus)) return ClockStatus;

  // Reenable clock
  MsdcSetBits (Private, MSDC_CFG, MSDC_CFG_CCKPD);

  // Needed because clock has been changed
  MsdcSetTimeout (Private);
  return EFI_SUCCESS;
}

VOID
MsdcCheckBusy (
  MSDC_PRIVATE_DATA* Private,
  BOOLEAN *IsBusy)
{
  UINT32 Reg;
  MsdcRead (Private, SDC_STS, &Reg);
  *IsBusy = (Reg & SDC_STS_BUSY);
}

#define MSDC_INT_CMDSTS (MSDC_INT_CMDRDY  | MSDC_INT_CMDTMO  | MSDC_INT_CMDCRCERR | \
                         MSDC_INT_ACMDRDY | MSDC_INT_ACMDTMO | MSDC_INT_ACMDCRCERR)
#define MSDC_INT_DATSTS (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR | MSDC_INT_XFER_COMPL)

EFI_STATUS
MsdcIntTrackError (
  MSDC_PRIVATE_DATA* Private,
  UINT32 IntMask,
  UINT32 IntStatus)
{
  // Clear interrupts
  MsdcWrite (Private, MSDC_INT, IntStatus & IntMask);

  if (IntStatus & MSDC_INT_CMDTMO) {
    return EFI_TIMEOUT;
  }

  if (IntStatus & MSDC_INT_CMDCRCERR) {
    return EFI_CRC_ERROR;
  }

  if (IntStatus & MSDC_INT_ACMDTMO) {
    return EFI_TIMEOUT;
  }

  if (IntStatus & MSDC_INT_ACMDCRCERR) {
    return EFI_CRC_ERROR;
  }

  if (IntStatus & MSDC_INT_DATTMO) {
    return EFI_TIMEOUT;
  }

  if (IntStatus & MSDC_INT_DATCRCERR) {
    return EFI_CRC_ERROR;
  }

  return EFI_DEVICE_ERROR;
}


// MT6768 uses an increasing ARM generic counter. Cap each hardware wait even
// when a caller requests an unlimited timeout; a broken card must return.
STATIC
BOOLEAN
MsdcDeadlineExpired (
  UINT64 Start,
  UINT64 TimeoutUs)
{
  return GetTimeInNanoSecond (GetPerformanceCounter () - Start) >= TimeoutUs * 1000;
}

STATIC
EFI_STATUS
MsdcTransferTimeout (
  MSDC_PRIVATE_DATA *Private,
  CONST CHAR8 *Phase,
  UINT32 Remaining)
{
  UINT32 IntStatus, Fifo, State, Cmd;
  MsdcRead (Private, MSDC_INT, &IntStatus);
  MsdcRead (Private, MSDC_FIFOCS, &Fifo);
  MsdcRead (Private, SDC_STS, &State);
  MsdcRead (Private, SDC_CMD, &Cmd);
  DEBUG ((DEBUG_ERROR,
    "[WOA][MSDC%u] %a timeout remaining=%u INT=%08x FIFO=%08x STS=%08x CMD=%08x\n",
    Private->Index, Phase, Remaining, IntStatus, Fifo, State, Cmd));
  return EFI_TIMEOUT;
}

EFI_STATUS
MsdcPollInterrupts (
  MSDC_PRIVATE_DATA *Private,
  UINT32 ExpectedInterrupts,
  UINT32 SuccessInterrupts,
  UINT64 TimeoutUs)
{
  UINT32 IntStatus;
  UINT64 Start = GetPerformanceCounter ();
  for (;;) {
    MsdcRead (Private, MSDC_INT, &IntStatus);
    // A completion bit must never hide a simultaneous CRC/timeout error.
    if (IntStatus & (ExpectedInterrupts & ~SuccessInterrupts))
      return MsdcIntTrackError (Private, ExpectedInterrupts, IntStatus);
    if (IntStatus & SuccessInterrupts) {
      MsdcWrite (Private, MSDC_INT, IntStatus & SuccessInterrupts);
      return EFI_SUCCESS;
    }
    if (MsdcDeadlineExpired (Start, TimeoutUs))
      return MsdcTransferTimeout (Private, "command", 0);
    MicroSecondDelay (1);
  }
}

VOID
MsdcFifoRxBytes (
  MSDC_PRIVATE_DATA* Private,
  UINT32 *RxBytes)
{
  UINT32 FifoCs;

  MsdcRead (Private, MSDC_FIFOCS, &FifoCs);
  *RxBytes = FifoCs & 0xff;
}

VOID
MsdcFifoTxBytes (
  MSDC_PRIVATE_DATA* Private,
  UINT32 *TxBytes)
{
  UINT32 FifoCs;

  MsdcRead (Private, MSDC_FIFOCS, &FifoCs);
  *TxBytes = (FifoCs >> 16) & 0xff;
}

VOID
MsdcFifoRead (
  MSDC_PRIVATE_DATA* Private,
  UINT8 *ByteBuffer,
  UINT32 BufferLength)
{
  UINT32 RemainSize = BufferLength;

  // Pointer align
  while ((UINTN)ByteBuffer % 4 != 0 && RemainSize > 0) {
    *ByteBuffer = MmioRead8 (Private->MsdcMmioReg + MSDC_RXDATA);
    ByteBuffer++;
    RemainSize--;
  }

  while (RemainSize >= 4) {
    *(UINT32 *)ByteBuffer = MmioRead32 (Private->MsdcMmioReg + MSDC_RXDATA);
    ByteBuffer += 4;
    RemainSize -= 4;
  }

  while (RemainSize) {
    *ByteBuffer = MmioRead8 (Private->MsdcMmioReg + MSDC_RXDATA);
    ByteBuffer++;
    RemainSize--;
  }
}

VOID
MsdcFifoWrite (
  MSDC_PRIVATE_DATA* Private,
  UINT8 *ByteBuffer,
  UINT32 BufferLength)
{
  UINT32 RemainSize = BufferLength;

  // Pointer align
  while ((UINTN)ByteBuffer % 4 != 0 && RemainSize > 0) {
    MmioWrite8 (Private->MsdcMmioReg + MSDC_TXDATA, *ByteBuffer);
    ByteBuffer++;
    RemainSize--;
  }

  while (RemainSize >= 4) {
    MmioWrite32 (Private->MsdcMmioReg + MSDC_TXDATA, *(UINT32 *)ByteBuffer);
    ByteBuffer += 4;
    RemainSize -= 4;
  }

  while (RemainSize) {
    MmioWrite8 (Private->MsdcMmioReg + MSDC_TXDATA, *ByteBuffer);
    ByteBuffer++;
    RemainSize--;
  }
}

EFI_STATUS
MsdcPioRead (
  MSDC_PRIVATE_DATA *Private,
  VOID *Buffer,
  UINT32 BufferLength,
  UINT64 TimeoutUs)
{
  UINT32 IntStatus, RemainSize = BufferLength, RxBytes;
  UINT8 *ByteBuffer = Buffer;
  UINT64 Start = GetPerformanceCounter ();
  BOOLEAN Complete = FALSE;
  MsdcClrBits (Private, MSDC_INTEN, MSDC_INT_DATSTS);
  for (;;) {
    MsdcRead (Private, MSDC_INT, &IntStatus);
    if (IntStatus & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR |
                    MSDC_INT_ACMDTMO | MSDC_INT_ACMDCRCERR))
      return MsdcIntTrackError (Private, MSDC_INT_DATSTS | MSDC_INT_CMDSTS, IntStatus);
    Complete |= (IntStatus & MSDC_INT_XFER_COMPL) != 0;
    MsdcFifoRxBytes (Private, &RxBytes);
    if (RxBytes > MSDC_FIFO_SIZE || RxBytes > RemainSize) {
      DEBUG ((DEBUG_ERROR, "[WOA][MSDC%u] invalid RX count=%u remaining=%u\n",
              Private->Index, RxBytes, RemainSize));
      return EFI_DEVICE_ERROR;
    }
    if (RxBytes != 0) {
      MsdcFifoRead (Private, ByteBuffer, RxBytes);
      ByteBuffer += RxBytes;
      RemainSize -= RxBytes;
    }
    // Completion can precede software draining the final FIFO contents.
    if (Complete && RemainSize == 0) {
      MsdcWrite (Private, MSDC_INT, MSDC_INT_XFER_COMPL);
      return EFI_SUCCESS;
    }
    if (Complete && RxBytes == 0) {
      DEBUG ((DEBUG_ERROR, "[WOA][MSDC%u] short PIO read: remaining=%u\n",
              Private->Index, RemainSize));
      return EFI_ABORTED;
    }
    if (MsdcDeadlineExpired (Start, TimeoutUs))
      return MsdcTransferTimeout (Private, "PIO read", RemainSize);
    if (RxBytes == 0) MicroSecondDelay (1);
  }
}

EFI_STATUS
MsdcPioWrite (
  MSDC_PRIVATE_DATA *Private,
  VOID *Buffer,
  UINT32 BufferLength,
  UINT64 TimeoutUs)
{
  UINT32 IntStatus, ChunkSize, RemainSize = BufferLength, TxBytes;
  UINT8 *ByteBuffer = Buffer;
  UINT64 Start = GetPerformanceCounter ();
  MsdcClrBits (Private, MSDC_INTEN, MSDC_INT_DATSTS);
  for (;;) {
    MsdcRead (Private, MSDC_INT, &IntStatus);
    if (IntStatus & (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR |
                    MSDC_INT_ACMDTMO | MSDC_INT_ACMDCRCERR))
      return MsdcIntTrackError (Private, MSDC_INT_DATSTS | MSDC_INT_CMDSTS, IntStatus);
    if (IntStatus & MSDC_INT_XFER_COMPL) {
      MsdcWrite (Private, MSDC_INT, MSDC_INT_XFER_COMPL);
      if (RemainSize != 0) {
        DEBUG ((DEBUG_ERROR, "[WOA][MSDC%u] short PIO write: remaining=%u\n",
                Private->Index, RemainSize));
        return EFI_ABORTED;
      }
      return EFI_SUCCESS;
    }
    MsdcFifoTxBytes (Private, &TxBytes);
    if (TxBytes > MSDC_FIFO_SIZE) {
      DEBUG ((DEBUG_ERROR, "[WOA][MSDC%u] invalid TX count=%u\n",
              Private->Index, TxBytes));
      return EFI_DEVICE_ERROR;
    }
    ChunkSize = RemainSize > MSDC_FIFO_SIZE ? MSDC_FIFO_SIZE : RemainSize;
    // TXCNT is a byte count, not the unshifted register mask. Guard the
    // subtraction and never enqueue data beyond the actual FIFO capacity.
    if (ChunkSize != 0 && ChunkSize <= MSDC_FIFO_SIZE - TxBytes) {
      MsdcFifoWrite (Private, ByteBuffer, ChunkSize);
      ByteBuffer += ChunkSize;
      RemainSize -= ChunkSize;
    } else {
      MicroSecondDelay (1);
    }
    if (MsdcDeadlineExpired (Start, TimeoutUs))
      return MsdcTransferTimeout (Private, "PIO write", RemainSize);
  }
}

EFI_STATUS
MsdcSendCmd (
  MSDC_PRIVATE_DATA* Private,
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *Packet)
{
  BOOLEAN IsBusy, IsDataTransfer, IsRead;
  EFI_STATUS Status;
  UINT32 RawCmd, RspType, BlkLen;
  UINT64 TimeoutUs = Packet->Timeout;
  STATIC UINT32 WriteTraces;
  BOOLEAN TraceWrite;
  if (TimeoutUs == 0) TimeoutUs = 5000000;
  if (TimeoutUs > 30000000) TimeoutUs = 30000000;
  EFI_SD_MMC_COMMAND_BLOCK *CommandBlk = Packet->SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK  *SdMmcStatusBlk = Packet->SdMmcStatusBlk;

  RawCmd = CommandBlk->CommandIndex;
  IsDataTransfer = FALSE;
  IsRead = TRUE;

  if (CommandBlk->CommandType != SdMmcCommandTypeBc) {
    switch (CommandBlk->ResponseType) {
      case SdMmcResponseTypeR1:
      case SdMmcResponseTypeR5:
      case SdMmcResponseTypeR6:
      case SdMmcResponseTypeR7:
        RspType = 1;
        break;
      case SdMmcResponseTypeR2:
        RspType = 2;
        break;
      case SdMmcResponseTypeR3:
        RspType = 3;
        break;
      case SdMmcResponseTypeR4:
        RspType = 4;
        break;
      case SdMmcResponseTypeR1b:
        RspType = 7;
        break;
      case SdMmcResponseTypeR5b:
      default:
        return EFI_INVALID_PARAMETER;
    }
    RawCmd |= RspType << SDC_CMD_RSP_TYPE_SHIFT;
  }

  if (CommandBlk->CommandIndex == SD_READ_SINGLE_BLOCK ||
      (CommandBlk->CommandIndex == EMMC_SEND_EXT_CSD && CommandBlk->CommandType == SdMmcCommandTypeAdtc)) {
    // single block transaction
    RawCmd |= SDC_CMD_SINGLE_BLK;
    IsDataTransfer = TRUE;

    BlkLen = 1;
  }

  if (CommandBlk->CommandIndex == SD_READ_MULTIPLE_BLOCK) {
    // multiple block transaction
    RawCmd |= SDC_CMD_MULTIPLE_BLK;
    IsDataTransfer = TRUE;
    // Enable auto sending of CMD12 for SDCard
    if (Private->SdInfo.CardType == SdCard) {
      RawCmd |= SDC_CMD_AUTO12;
    }

    BlkLen = Packet->InTransferLength / BLOCK_SIZE;
  }

  if (CommandBlk->CommandIndex == SD_WRITE_SINGLE_BLOCK) {
    // write mode
    RawCmd |= SDC_CMD_RW;
    // single block transaction
    RawCmd |= SDC_CMD_SINGLE_BLK;

    IsDataTransfer = TRUE;
    IsRead = FALSE;

    BlkLen = 1;
  }

  if (CommandBlk->CommandIndex == SD_WRITE_MULTIPLE_BLOCK) {
    // write mode
    RawCmd |= SDC_CMD_RW;
    // multiple block transaction
    RawCmd |= SDC_CMD_MULTIPLE_BLK;
    // Enable auto sending of CMD12 for SDCard
    if (Private->SdInfo.CardType == SdCard) {
      RawCmd |= SDC_CMD_AUTO12;
    }

    IsDataTransfer = TRUE;
    IsRead = FALSE;

    BlkLen = Packet->OutTransferLength / BLOCK_SIZE;
  }

  if (CommandBlk->CommandIndex == SD_STOP_TRANSMISSION) {
    // stop command
    RawCmd |= SDC_CMD_STOP_CMD;
  }

  if (IsDataTransfer) {
    RawCmd |= BLOCK_SIZE << SDC_CMD_BLK_SIZE_SHIFT;
    MsdcWrite (Private, SDC_BLK_NUM, BlkLen);
  }

  UINT32 BusyAttempts;
  for (BusyAttempts = 0; BusyAttempts < 50000; BusyAttempts++) {
    MsdcCheckBusy (Private, &IsBusy);
    if (!IsBusy) break;
    MicroSecondDelay (100);
  }
  if (IsBusy) {
    DEBUG ((DEBUG_ERROR, "[WOA][MSDC%u] command %u busy timeout\n", Private->Index, CommandBlk->CommandIndex));
    return EFI_TIMEOUT;
  }


  TraceWrite = IsDataTransfer && !IsRead && WriteTraces < 8;
  if (TraceWrite) {
    WriteTraces++;
    DEBUG ((DEBUG_INFO, "[WOA][MSDC%u] write begin cmd=%u lba=%08x bytes=%u\n",
            Private->Index, CommandBlk->CommandIndex, CommandBlk->CommandArgument,
            Packet->OutTransferLength));
  }
  // Clear stale command/data completion before submitting the next command.
  MsdcWrite (Private, MSDC_INT, MSDC_INT_CMDSTS | MSDC_INT_DATSTS);
  // Disable interrupts because we use polling way
  MsdcClrBits (Private, MSDC_INTEN, MSDC_INT_CMDSTS);

  MsdcWrite (Private, SDC_ARG, CommandBlk->CommandArgument);
  MsdcWrite (Private, SDC_CMD, RawCmd);

  Status = MsdcPollInterrupts (Private, MSDC_INT_CMDSTS, MSDC_INT_CMDRDY, TimeoutUs);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (CommandBlk->CommandType != SdMmcCommandTypeBc) {
    if (CommandBlk->ResponseType == SdMmcResponseTypeR2) {
      MsdcRead (Private, SDC_RESP0, &SdMmcStatusBlk->Resp0);
      MsdcRead (Private, SDC_RESP1, &SdMmcStatusBlk->Resp1);
      MsdcRead (Private, SDC_RESP2, &SdMmcStatusBlk->Resp2);
      MsdcRead (Private, SDC_RESP3, &SdMmcStatusBlk->Resp3);

      // Per SDHCI spec, R2 response registers contains [127:8] bits only
      CopyMem(&SdMmcStatusBlk->Resp0, (UINT8*)&SdMmcStatusBlk->Resp0 + 1, 15);
    } else {
      MsdcRead (Private, SDC_RESP0, &SdMmcStatusBlk->Resp0);
    }
  }

  if (IsDataTransfer) {
    if (IsRead) {
      Status = MsdcPioRead (Private, Packet->InDataBuffer, Packet->InTransferLength, TimeoutUs);
      if (EFI_ERROR(Status)) {
        return Status;
      }
    } else {
      Status = MsdcPioWrite (Private, Packet->OutDataBuffer, Packet->OutTransferLength, TimeoutUs);
      if (EFI_ERROR(Status)) {
        return Status;
      }
    }
  }

  if (TraceWrite) DEBUG ((DEBUG_INFO, "[WOA][MSDC%u] write complete\n", Private->Index));
  return Status;
}

EFI_STATUS MsdcInit (
  MSDC_PRIVATE_DATA *Private)
{
  InitGpio (Private->Index);
  ClockControl (Private->Index, TRUE);

  // Configure to SD/MMC mode, Clock free running, PIO mode
  MsdcSetBits (Private, MSDC_CFG, MSDC_CFG_MODE | MSDC_CFG_CCKPD | MSDC_CFG_PIO);

  // SW Reset
  EFI_STATUS Status = MsdcReset (Private);
  if (EFI_ERROR (Status)) return Status;

  // Clear FIFO
  Status = MsdcClearFifo (Private);
  if (EFI_ERROR (Status)) return Status;

  // Mask all interrupts
  MsdcClearInterrupts (Private);

  // Enable SDIO mode, otherwise cmd5 won't work
  // This bit disables R4 response CRC check for SDIO card
  MsdcSetBits (Private, SDC_CFG, SDC_CFG_SDIO);
  // Disable detecting SDIO interrupts
  MsdcClrBits (Private, SDC_CFG, SDC_CFG_SDIOIDE);

  if (gPlatformInfo.UseTop) {
    MsdcTopWrite (Private, TOP_CTRL, 0);
    MsdcTopWrite (Private, TOP_CMD, 0);
  } else {
    MsdcWrite (Private, gPlatformInfo.MsdcPadTuneReg, 0);
  }

  MsdcWrite (Private, MSDC_IOCON, 0);

  // Quirks
  MsdcWrite (Private, MSDC_PATCH_BIT0, 0x403c0446);
  MsdcWrite (Private, MSDC_PATCH_BIT1, 0xffff4089);

  MsdcSetBits (Private, EMMC50_CFG0, EMMC50_CFG0_CRCSTSSEL);

  if (gPlatformInfo.BusyCheck) {
    MsdcClrBits (Private, MSDC_PATCH_BIT1, MSDC_PB1_BUSYCHECKSEL);
  }

  if (gPlatformInfo.StopClkFix) {
    MsdcSetBits (Private, MSDC_PATCH_BIT1, BIT8 | BIT9);
    MsdcClrBits (Private, SDC_FIFO_CFG, SDC_FIFO_CFG_WRVALIDSEL);
    MsdcClrBits (Private, SDC_FIFO_CFG, SDC_FIFO_CFG_RDVALIDSEL);
  }

  if (gPlatformInfo.AsyncFifo)
  {
    MsdcClrSetBits(Private, MSDC_PATCH_BIT2, MSDC_PB2_RESPWAIT, 3 << MSDC_PB2_RESPWAIT_SHIFT);

    MsdcClrBits (Private, MSDC_PATCH_BIT2, MSDC_PB2_CFGRESP);
    MsdcSetBits (Private, MSDC_PATCH_BIT2, MSDC_PB2_CFGCRCSTS);

    if (gPlatformInfo.EnhanceRx)
    {
      if (gPlatformInfo.UseTop) {
        MsdcTopSetBits (Private, TOP_CTRL, SDC_RX_ENH_EN);
      } else {
        MsdcSetBits (Private, SDC_ADV_CFG0, SDC_RX_ENHANCE_EN);
      }
    } else {
      MsdcClrSetBits(Private, MSDC_PATCH_BIT2, MSDC_PB2_RESPSTSENSEL | MSDC_PB2_CRCSTSENSEL, (2 << 16) | (2 << 29));
    }
  }

  // Data tune
  if (gPlatformInfo.UseTop) {
    MsdcTopSetBits (Private, TOP_CTRL, PAD_DAT_RD_RXDLY_SEL);
    MsdcTopClrBits (Private, TOP_CTRL, DATA_K_VALUE_SEL);
    MsdcTopSetBits (Private, TOP_CMD, PAD_CMD_RD_RXDLY_SEL);
    if (gPlatformInfo.TuningStep[Private->Index] > 32)
    {
      MsdcTopSetBits (Private, TOP_CTRL, PAD_DAT_RD_RXDLY2_SEL);
      MsdcTopSetBits (Private, TOP_CMD, PAD_CMD_RD_RXDLY2_SEL);
    }
  } else {
    MsdcSetBits (Private, gPlatformInfo.MsdcPadTuneReg, MSDC_PAD_TUNE_RD_SEL | MSDC_PAD_TUNE_CMD_SEL);
  }

  // Set default data timeout
  Private->HostData.TimeoutNs = 100000000;
  Private->HostData.TimeoutClks = 3 * (1 << SCLK_CYCLES_SHIFT);

  // Set default bus width
  MsdcSetBusWidth (Private, 1);
  DEBUG ((DEBUG_INFO, "[WOA][MSDC%u] bounded PIO / TXCNT / completion fixes active\n", Private->Index));
  return EFI_SUCCESS;
}

EFI_STATUS
CardReset (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru)
{
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;

  SdMmcCmdBlk.CommandIndex = SD_GO_IDLE_STATE;
  SdMmcCmdBlk.CommandType  = SdMmcCommandTypeBc;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);

  return Status;
}

EFI_STATUS
SdSendIfCond (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru)
{
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_STATUS Status;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;

  SdMmcCmdBlk.CommandIndex    = SD_SEND_IF_COND;
  SdMmcCmdBlk.CommandType     = SdMmcCommandTypeBcr;
  SdMmcCmdBlk.ResponseType    = SdMmcResponseTypeR7;
  SdMmcCmdBlk.CommandArgument = 0x1AA;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  return Status;
}

EFI_STATUS
SdCardSendOpCond (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru,
  UINT16                         Rca,
  UINT32                         VoltageWindow,
  BOOLEAN                        S18R,
  BOOLEAN                        Xpc,
  BOOLEAN                        Hcs,
  UINT32                        *Ocr)
{
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;
  UINT32 Switch;
  UINT32 MaxPower;
  UINT32 HostCapacity;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;

  SdMmcCmdBlk.CommandIndex    = SD_APP_CMD;
  SdMmcCmdBlk.CommandType     = SdMmcCommandTypeAc;
  SdMmcCmdBlk.ResponseType    = SdMmcResponseTypeR1;
  SdMmcCmdBlk.CommandArgument = (UINT32)Rca << 16;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  SdMmcCmdBlk.CommandIndex = SD_SEND_OP_COND;
  SdMmcCmdBlk.CommandType  = SdMmcCommandTypeBcr;
  SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR3;

  Switch       = S18R ? BIT24 : 0;
  MaxPower     = Xpc ? BIT28 : 0;
  HostCapacity = Hcs ? BIT30 : 0;

  SdMmcCmdBlk.CommandArgument = (VoltageWindow & 0xFFFFFF) | Switch | \
                                MaxPower | HostCapacity;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);

  if (!EFI_ERROR (Status)) {
    *Ocr = SdMmcStatusBlk.Resp0;
  }

  return Status;
}

EFI_STATUS
EMMCSendOpCond (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru,
  UINT32                         VoltageWindow,
  BOOLEAN                        Hcs,
  UINT32                        *Ocr)
{
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;
  UINT32 HostCapacity;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;

  SdMmcCmdBlk.CommandIndex = EMMC_SEND_OP_COND;
  SdMmcCmdBlk.CommandType  = SdMmcCommandTypeBcr;
  SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR3;

  HostCapacity = Hcs ? BIT30 : 0;

  SdMmcCmdBlk.CommandArgument = (VoltageWindow & 0xFFFFFF) | HostCapacity;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);

  if (!EFI_ERROR (Status)) {
    *Ocr = SdMmcStatusBlk.Resp0;
  }

  return Status;
}

EFI_STATUS
CardAllSendCid (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru)
{
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;

  SdMmcCmdBlk.CommandIndex = SD_ALL_SEND_CID;
  SdMmcCmdBlk.CommandType  = SdMmcCommandTypeBcr;
  SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR2;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);

  return Status;
}

EFI_STATUS
SdCardSetRca (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru,
  UINT16                        *Rca)
{
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;

  SdMmcCmdBlk.CommandIndex = SD_SET_RELATIVE_ADDR;
  SdMmcCmdBlk.CommandType  = SdMmcCommandTypeBcr;
  SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR6;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);
  if (!EFI_ERROR (Status)) {
    *Rca = (UINT16)(SdMmcStatusBlk.Resp0 >> 16);
  }

  return Status;
}

EFI_STATUS
EMMCSetRca (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru,
  UINT16                         Rca)
{
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;

  SdMmcCmdBlk.CommandIndex = EMMC_SET_RELATIVE_ADDR;
  SdMmcCmdBlk.CommandType  = SdMmcCommandTypeAc;
  SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR1;
  SdMmcCmdBlk.CommandArgument = (UINT32)Rca << 16;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);

  return Status;
}

EFI_STATUS
CardSelect (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru,
  UINT16                         Rca)
{
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;

  SdMmcCmdBlk.CommandIndex = SD_SELECT_DESELECT_CARD;
  SdMmcCmdBlk.CommandType  = SdMmcCommandTypeAc;
  if (Rca != 0) {
    SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR1b;
  }

  SdMmcCmdBlk.CommandArgument = (UINT32)Rca << 16;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);

  return Status;
}

EFI_STATUS
CardSendStatus (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru,
  UINT16                         Rca,
  UINT32                        *DevStatus)
{
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;

  SdMmcCmdBlk.CommandIndex    = SD_SEND_STATUS;
  SdMmcCmdBlk.CommandType     = SdMmcCommandTypeAc;
  SdMmcCmdBlk.ResponseType    = SdMmcResponseTypeR1;
  SdMmcCmdBlk.CommandArgument = (UINT32)Rca << 16;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);
  if (!EFI_ERROR (Status)) {
    *DevStatus = SdMmcStatusBlk.Resp0;
  }

  return Status;
}

EFI_STATUS
SdCardSwitch (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru,
  UINT8                          AccessMode,
  UINT8                          CommandSystem,
  UINT8                          DriveStrength,
  UINT8                          PowerLimit,
  BOOLEAN                        Mode,
  UINT8                         *SwitchResp)
{
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;
  UINT32 ModeValue;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;

  SdMmcCmdBlk.CommandIndex = SD_SWITCH_FUNC;
  SdMmcCmdBlk.CommandType  = SdMmcCommandTypeAdtc;
  SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR1;

  ModeValue                   = Mode ? BIT31 : 0;
  SdMmcCmdBlk.CommandArgument = (AccessMode & 0xF) |            \
                                ((PowerLimit & 0xF) << 4) |     \
                                ((DriveStrength & 0xF) << 8) |  \
                                ((DriveStrength & 0xF) << 12) | \
                                ModeValue;

  Packet.InDataBuffer     = SwitchResp;
  Packet.InTransferLength = 64;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);

  return Status;
}

EFI_STATUS
EMMCSwitch (
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru,
  UINT8 Access,
  UINT8 Index,
  UINT8 Value,
  UINT8 CmdSet)
{
  EFI_SD_MMC_COMMAND_BLOCK SdMmcCmdBlk;
  EFI_SD_MMC_STATUS_BLOCK SdMmcStatusBlk;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET Packet;
  EFI_STATUS Status;

  ZeroMem (&SdMmcCmdBlk, sizeof (SdMmcCmdBlk));
  ZeroMem (&SdMmcStatusBlk, sizeof (SdMmcStatusBlk));
  ZeroMem (&Packet, sizeof (Packet));

  Packet.SdMmcCmdBlk    = &SdMmcCmdBlk;
  Packet.SdMmcStatusBlk = &SdMmcStatusBlk;

  SdMmcCmdBlk.CommandIndex = EMMC_SWITCH;
  SdMmcCmdBlk.CommandType  = SdMmcCommandTypeAc;
  SdMmcCmdBlk.ResponseType = SdMmcResponseTypeR1b;

  SdMmcCmdBlk.CommandArgument = (Access << 24) |
                                (Index << 16) |
                                (Value << 8) |
                                CmdSet;

  Status = MsdcPassThru (PassThru, 0, &Packet, NULL);

  return Status;
}

EFI_STATUS
CardSetBusMode (
  MSDC_PRIVATE_DATA *Private,
  UINT16             Rca)
{
  EFI_STATUS Status;
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru;
  UINT8 SwitchResp[64];
  UINT32 DevStatus;

  PassThru = &Private->PassThru;

  Status = CardSelect(PassThru, Rca);
  if (EFI_ERROR(Status))
  {
    DEBUG ((DEBUG_ERROR, "MsdcDxe: CardSelect failed with status %r\n", Status));
    return Status;
  }

  if (Private->SdInfo.CardType == SdCard)
  {
    Status = SdCardSwitch(PassThru, 1, 0xF, 0xF, 0xF, TRUE, SwitchResp);
    if (EFI_ERROR(Status))
    {
      DEBUG ((DEBUG_ERROR, "MsdcDxe: SdCardSwitch failed with status %r\n", Status));
      return Status;
    }
  } else {
    Status = EMMCSwitch(PassThru, 3, 185, 1, 0);
    if (EFI_ERROR(Status))
    {
      DEBUG ((DEBUG_ERROR, "MsdcDxe: EMMCSwitch failed with status %r\n", Status));
      return Status;
    }
  }

  Status = CardSendStatus(PassThru, Rca, &DevStatus);
  if (EFI_ERROR(Status))
  {
    DEBUG ((DEBUG_ERROR, "MsdcDxe: CardSendStatus failed with status %r\n", Status));
    return Status;
  }

  if (DevStatus & BIT7) {
    DEBUG ((DEBUG_ERROR, "MsdcDxe: Got switch error, DevStatus is 0x%x\n", DevStatus));
    return EFI_DEVICE_ERROR;
  }

  return MsdcSetMclk (Private, 50 * 1000 * 1000);
}

EFI_STATUS
EmmcIdentification (
  MSDC_PRIVATE_DATA *Private)
{
  EFI_STATUS Status;
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru;
  UINT32 Ocr;

  PassThru = &Private->PassThru;

  PowerControl (Private->Index, TRUE);
  MicroSecondDelay (20000);
  Status = MsdcSetMclk (Private, 400000);
  if (EFI_ERROR (Status)) return Status;

  Status = CardReset (PassThru);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MsdcDxe: CardReset failed with status %r\n", Status));
    return Status;
  }

  Ocr = 0;

  UINT32 OpAttempts = 0;
  do {
    if (++OpAttempts > 20) {
      DEBUG ((DEBUG_ERROR, "[WOA][MSDC%u] eMMC OCR ready timeout\n", Private->Index));
      return EFI_TIMEOUT;
    }
    Status = EMMCSendOpCond (PassThru, Ocr, TRUE, &Ocr);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "MsdcDxe: EMMCSendOpCond failed with status %r\n", Status));
      return Status;
    }

    MicroSecondDelay (100000);
  } while ((Ocr & BIT31) == 0);

  Status = CardAllSendCid (PassThru);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MsdcDxe: CardAllSendCid failed with status %r\n", Status));
    return Status;
  }

  Status = EMMCSetRca (PassThru, 1);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MsdcDxe: EMMCSetRca failed with status %r\n", Status));
    return Status;
  }

  Private->SdInfo.CardType = EmmcCard;
  DEBUG ((DEBUG_ERROR, "MsdcDxe: Found EMMC device at controller with index %d\n", Private->Index));

  Status = CardSetBusMode (Private, 1);

  return Status;
}

EFI_STATUS
SdCardIdentification (
  MSDC_PRIVATE_DATA *Private)
{
  EFI_STATUS Status;
  EFI_SD_MMC_PASS_THRU_PROTOCOL *PassThru;
  UINT32 Ocr;
  UINT16 Rca;
  BOOLEAN S18r;
  BOOLEAN Xpc;
  BOOLEAN Hcs;

  PassThru = &Private->PassThru;

  PowerControl (Private->Index, TRUE);
  MicroSecondDelay (20000);
  Status = MsdcSetMclk (Private, 400000);
  if (EFI_ERROR (Status)) return Status;

  Status = CardReset (PassThru);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MsdcDxe: CardReset failed with status %r\n", Status));
    return Status;
  }

  MicroSecondDelay (10000);

  Status = SdSendIfCond (PassThru);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MsdcDxe: SdSendIfCond failed with status %r\n", Status));
    return Status;
  }

  Status = SdCardSendOpCond (PassThru, 0, 0, FALSE, FALSE, FALSE, &Ocr);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MsdcDxe: SdCardSendOpCond failed with status %r\n", Status));
    return Status;
  }

  S18r = FALSE;
  Xpc = TRUE;
  Hcs = TRUE;

  UINT32 OpAttempts = 0;
  do {
    if (++OpAttempts > 100) {
      DEBUG ((DEBUG_ERROR, "[WOA][MSDC%u] SD OCR ready timeout\n", Private->Index));
      return EFI_TIMEOUT;
    }
    Status = SdCardSendOpCond (PassThru, 0, Ocr, S18r, Xpc, Hcs, &Ocr);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "MsdcDxe: SdCardSendOpCond failed with status %r\n", Status));
      return Status;
    }
    MicroSecondDelay(10000);
  } while ((Ocr & BIT31) == 0);

  Status = CardAllSendCid (PassThru);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MsdcDxe: CardAllSendCid failed with status %r\n", Status));
    return Status;
  }

  Status = SdCardSetRca (PassThru, &Rca);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MsdcDxe: CardAllSendCid failed with status %r\n", Status));
    return Status;
  }

  Private->SdInfo.CardType = SdCard;
  DEBUG ((DEBUG_ERROR, "MsdcDxe: Found SD device at controller with index %d\n", Private->Index));

  Status = CardSetBusMode (Private, Rca);

  return Status;
}

CARD_DETECT_ROUTINE CardDetectRoutineTable[] = {
  EmmcIdentification,
  SdCardIdentification,
  NULL
};

EFI_STATUS
InitMsdc (
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  MSDC_PRIVATE_DATA* Private;
  MSDC_DEVICE_PATH* DevicePath;
  CARD_DETECT_ROUTINE *Routine;
  EFI_MEMORY_REGION_DESCRIPTOR Region;
  CHAR8 MsdcName[11];

  for (UINTN i = 0; i < gPlatformInfo.NumberOfHosts; i++) {
    Private = AllocateCopyPool (sizeof(MSDC_PRIVATE_DATA), &gMSDCPrivateDataTemplate);
    if (Private == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    DevicePath = AllocateCopyPool (sizeof(MSDC_DEVICE_PATH), &gMSDCDevicePathTemplate);
    if (DevicePath == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    DevicePath->Mmc.Guid.Data4[7] = i;

    Private->Index = i;

    ZeroMem(MsdcName, sizeof(MsdcName));
    AsciiSPrint(MsdcName, sizeof(MsdcName), "MSDC-%u", i);

    Status = LocateMemoryRegionByName (MsdcName, &Region);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to Locate %a Memory Region! Status = %r\n", MsdcName, Status));
      continue;
    }

    Private->MsdcMmioReg = Region.Address;

    ZeroMem(MsdcName, sizeof(MsdcName));
    AsciiSPrint(MsdcName, sizeof(MsdcName), "MSDC Top-%u", i);

    Status = LocateMemoryRegionByName (MsdcName, &Region);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "Failed to Locate %a Memory Region! Status = %r\n", MsdcName, Status));
      continue;
    }

    Private->TopMmioReg = Region.Address;

    Status = MsdcInit (Private);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "[WOA][MSDC%u] initialization failed: %r\n", Private->Index, Status));
      FreePool (Private);
      FreePool (DevicePath);
      continue;
    }

    for (UINT32 j = 0; j < sizeof(CardDetectRoutineTable) / sizeof(CARD_DETECT_ROUTINE); j++) {
      Routine = &CardDetectRoutineTable[j];
      if (*Routine != NULL) {
        Status = (*Routine)(Private);
        if (!EFI_ERROR(Status)) {
          break;
        }
      }
    }

    if (EFI_ERROR(Status)) {
      DEBUG ((DEBUG_ERROR, "MsdcDxe: Failed to detect card! %r\n", Status));
      Status = EFI_SUCCESS;
      FreePool (Private);
      FreePool (DevicePath);
      continue;
    }

    Status = gBS->InstallMultipleProtocolInterfaces (
                    &Private->ControllerHandle,
                    &gEfiDevicePathProtocolGuid, DevicePath,
                    &gEfiSdMmcPassThruProtocolGuid, &(Private->PassThru),
                    NULL
                    );
    if (EFI_ERROR (Status)) {
      FreePool (DevicePath);
      FreePool (Private);
      continue;
    }
  }

  return Status;
}
