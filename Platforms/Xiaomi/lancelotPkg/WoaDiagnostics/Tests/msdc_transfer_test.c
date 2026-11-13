/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * The controller model asserts FIFO capacity and data integrity. The driver
 * functions themselves are extracted from the firmware being built.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int BOOLEAN;
typedef int EFI_STATUS;
typedef char CHAR8;
typedef void VOID;
typedef void *EFI_EVENT;
typedef struct { UINT32 Index; } MSDC_PRIVATE_DATA;
typedef struct { int unused; } EFI_SD_MMC_PASS_THRU_PROTOCOL;
typedef struct {
  void *SdMmcCmdBlk, *SdMmcStatusBlk, *OutDataBuffer, *InDataBuffer;
  UINT32 OutTransferLength, InTransferLength;
  EFI_STATUS TransactionStatus;
} EFI_SD_MMC_PASS_THRU_COMMAND_PACKET;

#define CONST const
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS 0
#define EFI_TIMEOUT 1
#define EFI_CRC_ERROR 2
#define EFI_DEVICE_ERROR 3
#define EFI_ABORTED 4
#define EFI_INVALID_PARAMETER 5
#define EFI_ERROR(x) ((x) != EFI_SUCCESS)
#define DEBUG_ERROR 1
#define DEBUG(x) debug_noop x
#define MSDC_PRIVATE_FROM_THIS(x) (&controller)
/* MT6768 register layout, independent of the implementation under test. */
#define MSDC_INT 0x0c
#define MSDC_INTEN 0x10
#define MSDC_FIFOCS 0x14
#define SDC_CMD 0x34
#define SDC_STS 0x3c
#define MSDC_INT_ACMDRDY (1u << 3)
#define MSDC_INT_ACMDTMO (1u << 4)
#define MSDC_INT_ACMDCRCERR (1u << 5)
#define MSDC_INT_CMDRDY (1u << 8)
#define MSDC_INT_CMDTMO (1u << 9)
#define MSDC_INT_CMDCRCERR (1u << 10)
#define MSDC_INT_XFER_COMPL (1u << 12)
#define MSDC_INT_DATTMO (1u << 14)
#define MSDC_INT_DATCRCERR (1u << 15)
#define MSDC_INT_CMDSTS (MSDC_INT_CMDRDY | MSDC_INT_CMDTMO | MSDC_INT_CMDCRCERR | \
                        MSDC_INT_ACMDRDY | MSDC_INT_ACMDTMO | MSDC_INT_ACMDCRCERR)
#define MSDC_INT_DATSTS (MSDC_INT_DATTMO | MSDC_INT_DATCRCERR | MSDC_INT_XFER_COMPL)
#define MSDC_FIFO_SIZE 128u

static MSDC_PRIVATE_DATA controller;
static UINT32 interrupts, tx_count, rx_count, polls, target;
static UINT32 wrote, read_bytes, writes, reads, signals, sends;
static int drain_tx, supply_rx, complete_tx, free_on_signal;
static UINT64 clock_ns;
static EFI_STATUS command_status;
static unsigned char payload[2048], observed[2048];

static void debug_noop(int level, const char *format, ...) {
  (void)level; (void)format;
}
static UINT64 GetPerformanceCounter(void) { clock_ns += 1000; return clock_ns; }
static UINT64 GetTimeInNanoSecond(UINT64 ticks) { return ticks; }
static void MicroSecondDelay(unsigned us) { clock_ns += (UINT64)us * 1000; }
static void MsdcRead(MSDC_PRIVATE_DATA *p, UINT32 reg, UINT32 *out) {
  (void)p;
  if (reg == MSDC_INT) {
    ++polls;
    /* Start full, hold for two polls, then drain in partial FIFO units. */
    if (drain_tx && polls > 2 && tx_count)
      tx_count -= tx_count < 32 ? tx_count : 32;
    if (complete_tx && wrote == target && tx_count == 0)
      interrupts |= MSDC_INT_XFER_COMPL;
    if (supply_rx && rx_count == 0 && read_bytes < target) {
      rx_count = target - read_bytes;
      if (rx_count > MSDC_FIFO_SIZE) rx_count = MSDC_FIFO_SIZE;
      if (read_bytes + rx_count == target) interrupts |= MSDC_INT_XFER_COMPL;
    }
    *out = interrupts;
  } else if (reg == MSDC_FIFOCS) {
    *out = (tx_count << 16) | rx_count;
  } else {
    assert(reg == SDC_CMD || reg == SDC_STS);
    *out = 0;
  }
}
static void MsdcWrite(MSDC_PRIVATE_DATA *p, UINT32 reg, UINT32 value) {
  (void)p; assert(reg == MSDC_INT);
  interrupts &= ~value;
}
static void MsdcClrBits(MSDC_PRIVATE_DATA *p, UINT32 reg, UINT32 bits) {
  (void)p; (void)bits; assert(reg == MSDC_INTEN);
}
static void MsdcFifoWrite(MSDC_PRIVATE_DATA *p, VOID *data, UINT32 size) {
  (void)p;
  assert(size != 0 && size <= MSDC_FIFO_SIZE - tx_count);
  assert(wrote + size <= target && target <= sizeof(observed));
  memcpy(observed + wrote, data, size);
  wrote += size; tx_count += size; ++writes;
}
static void MsdcFifoRead(MSDC_PRIVATE_DATA *p, VOID *data, UINT32 size) {
  (void)p;
  assert(size != 0 && size <= rx_count);
  assert(read_bytes + size <= target && target <= sizeof(payload));
  memcpy(data, payload + read_bytes, size);
  read_bytes += size; rx_count -= size; ++reads;
}
static EFI_STATUS MsdcSendCmd(MSDC_PRIVATE_DATA *p, EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *packet) {
  (void)p; (void)packet; ++sends; return command_status;
}
static EFI_STATUS signal_event(EFI_EVENT event) {
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *packet = event;
  assert(packet->TransactionStatus == EFI_SUCCESS);
  ++signals;
  /* EDK2's callback may release the request immediately. */
  if (free_on_signal) free(packet);
  return EFI_SUCCESS;
}
struct host_boot_services { EFI_STATUS (*SignalEvent)(EFI_EVENT); };
static struct host_boot_services services = {signal_event};
static const struct host_boot_services *gBS = &services;

#include "msdc-under-test.inc"

static void reset(void) {
  interrupts = tx_count = rx_count = polls = target = 0;
  wrote = read_bytes = writes = reads = signals = sends = 0;
  drain_tx = supply_rx = complete_tx = free_on_signal = 0;
  command_status = EFI_SUCCESS; clock_ns = 0;
  memset(observed, 0, sizeof(observed));
  for (unsigned i = 0; i < sizeof(payload); ++i) payload[i] = (unsigned char)(i * 17u + 3u);
}

int main(void) {
  UINT32 count;
  reset(); tx_count = 128;
  MsdcFifoTxBytes(&controller, &count); assert(count == 128);
  tx_count = 64; MsdcFifoTxBytes(&controller, &count); assert(count == 64);
  puts("PASS: TXCNT is a byte count, including a full 128-byte FIFO");

  reset(); target = 512; tx_count = 128; drain_tx = complete_tx = 1;
  assert(MsdcPioWrite(&controller, payload, target, 1000) == EFI_SUCCESS);
  assert(wrote == target && writes == 4 && memcmp(payload, observed, target) == 0);
  puts("PASS: full/partly-full TX FIFO is never overfilled; all bytes match");

  reset(); target = 512; tx_count = 128;
  assert(MsdcPioWrite(&controller, payload, target, 50) == EFI_TIMEOUT && writes == 0);
  reset(); target = 512; drain_tx = 1;
  assert(MsdcPioWrite(&controller, payload, target, 100) == EFI_TIMEOUT);
  assert(wrote == target);
  reset(); target = 512; tx_count = 129;
  assert(MsdcPioWrite(&controller, payload, target, 100) == EFI_DEVICE_ERROR && writes == 0);
  puts("PASS: stuck FIFO, missing completion and impossible FIFO count return errors");

  reset(); target = 512; supply_rx = 1;
  assert(MsdcPioRead(&controller, observed, target, 1000) == EFI_SUCCESS);
  assert(reads == 4 && memcmp(observed, payload, target) == 0);
  reset(); target = 64; rx_count = 64; interrupts = MSDC_INT_XFER_COMPL;
  assert(MsdcPioRead(&controller, observed, target, 100) == EFI_SUCCESS);
  assert(read_bytes == 64 && memcmp(observed, payload, target) == 0);
  reset(); target = 512; interrupts = MSDC_INT_XFER_COMPL;
  assert(MsdcPioRead(&controller, observed, target, 100) == EFI_ABORTED);
  reset(); target = 512;
  assert(MsdcPioRead(&controller, observed, target, 50) == EFI_TIMEOUT);
  puts("PASS: RX completion drains pending bytes; short/stalled reads fail");

  reset(); interrupts = MSDC_INT_CMDRDY;
  assert(MsdcPollInterrupts(&controller, MSDC_INT_CMDSTS, MSDC_INT_CMDRDY, 50) == EFI_SUCCESS);
  reset(); interrupts = MSDC_INT_CMDRDY | MSDC_INT_CMDCRCERR;
  assert(MsdcPollInterrupts(&controller, MSDC_INT_CMDSTS, MSDC_INT_CMDRDY, 50) == EFI_CRC_ERROR);
  reset();
  assert(MsdcPollInterrupts(&controller, MSDC_INT_CMDSTS, MSDC_INT_CMDRDY, 50) == EFI_TIMEOUT);
  reset(); interrupts = MSDC_INT_XFER_COMPL | MSDC_INT_DATCRCERR; target = 128;
  assert(MsdcPioWrite(&controller, payload, target, 50) == EFI_CRC_ERROR);
  interrupts = MSDC_INT_XFER_COMPL | MSDC_INT_DATCRCERR;
  assert(MsdcPioRead(&controller, observed, target, 50) == EFI_CRC_ERROR);
  puts("PASS: command waits are bounded; simultaneous CRC errors win over completion");

  reset();
  EFI_SD_MMC_PASS_THRU_PROTOCOL protocol = {0};
  int dummy;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET packet = {
    .SdMmcCmdBlk = &dummy, .SdMmcStatusBlk = &dummy,
    .TransactionStatus = EFI_ABORTED
  };
  assert(MsdcPassThru(&protocol, 0, &packet, &packet) == EFI_SUCCESS);
  assert(signals == 1 && sends == 1 && packet.TransactionStatus == EFI_SUCCESS);
  command_status = EFI_CRC_ERROR;
  assert(MsdcPassThru(&protocol, 0, &packet, &packet) == EFI_CRC_ERROR);
  assert(signals == 1 && sends == 2 && packet.TransactionStatus == EFI_CRC_ERROR);
  assert(MsdcPassThru(&protocol, 1, &packet, NULL) == EFI_INVALID_PARAMETER && sends == 2);
  command_status = EFI_SUCCESS;
  EFI_SD_MMC_PASS_THRU_COMMAND_PACKET *heap = malloc(sizeof(*heap));
  assert(heap != NULL); *heap = packet; free_on_signal = 1;
  assert(MsdcPassThru(&protocol, 0, heap, heap) == EFI_SUCCESS && signals == 2);
  puts("PASS: success signals completion once; immediate failure cannot double-free a request");
  return 0;
}
