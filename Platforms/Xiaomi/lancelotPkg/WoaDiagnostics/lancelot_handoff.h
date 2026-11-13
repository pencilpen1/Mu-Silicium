/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * Shared little-endian firmware/Windows diagnostic contract, version 1.
 * Include after Uefi.h or ntddk.h (UINT32/UINT64 definitions).
 */
#ifndef LANCELOT_WOA_HANDOFF_H
#define LANCELOT_WOA_HANDOFF_H

#define LANCELOT_HANDOFF_BASE  0x512f0000ULL
#define LANCELOT_HANDOFF_SIZE  0x1000U
#define LANCELOT_HANDOFF_MAGIC 0x31474f4c414f574cULL /* LWOALOG1 */
#define LANCELOT_HANDOFF_VERSION 1U
#define LANCELOT_FW_LOG_BASE   0x512c0000ULL
#define LANCELOT_FW_LOG_SIZE   0x30000U
#define LANCELOT_USB_BASE      0x11200000ULL
#define LANCELOT_USB_SIZE      0x1000U
#define LANCELOT_USB_GSI       129U
#define LANCELOT_UART_BASE     0x11002000ULL
#define LANCELOT_UART_BAUD     115200U
#define LANCELOT_FLAG_USB_INITIALIZED 1U
#define LANCELOT_FLAG_EBS             2U
#define LANCELOT_FLAG_WINDOWS        4U

#define LANCELOT_STAGE_DXE         10U
#define LANCELOT_STAGE_READY       20U
#define LANCELOT_STAGE_PREFLIGHT   30U
#define LANCELOT_STAGE_STARTIMAGE  40U
#define LANCELOT_STAGE_EBS         50U
#define LANCELOT_STAGE_DRIVER      60U
#define LANCELOT_STAGE_USB         70U
#define LANCELOT_STAGE_USER        80U

#pragma pack(push, 8)
typedef struct {
  UINT64 Magic;
  UINT32 Version;
  UINT32 Size;
  UINT64 BootId;
  UINT32 Stage;
  UINT32 Flags;
  UINT64 FirmwareLogBase;
  UINT32 FirmwareLogSize;
  UINT32 Reserved0;
  UINT64 UsbBase;
  UINT32 UsbSize;
  UINT32 UsbGsi;
  UINT64 UartBase;
  UINT32 UartBaud;
  UINT32 Reserved1;
  UINT64 FrameBufferBase;
  UINT64 FrameBufferSize;
  UINT32 Width;
  UINT32 Height;
  UINT32 Stride;
  UINT32 LastStatus;
  UINT64 CounterFrequency;
  UINT64 Reserved2;
} LANCELOT_HANDOFF;
#pragma pack(pop)

#endif
