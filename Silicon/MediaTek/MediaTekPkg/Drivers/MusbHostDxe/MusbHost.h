/** @file
  MT6768 MUSB PIO host. Register definitions follow the pinned Linux sources
  listed in README.md, including MediaTek target/toggle register extensions.
  SPDX-License-Identifier: GPL-2.0-only
**/
#ifndef MT6768_MUSB_HOST_H
#define MT6768_MUSB_HOST_H

#include <Uefi.h>
#include <Protocol/Usb2HostController.h>
#include <Protocol/MtkUsbRole.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/BootLogLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define MUSB_BASE       0x11200000U
#define USB_PHY_BASE    0x11CC0800U
#define REG_POWER      0x01
#define REG_INTRTX     0x02
#define REG_INTRRX     0x04
#define REG_INTRTXE    0x06
#define REG_INTRRXE    0x08
#define REG_INTRUSB    0x0A
#define REG_INTRUSBE   0x0B
#define REG_FRAME      0x0C
#define REG_INDEX      0x0E
#define REG_TXMAXP     0x10
#define REG_TXCSR      0x12
#define REG_RXMAXP     0x14
#define REG_RXCSR      0x16
#define REG_RXCOUNT    0x18
#define REG_TXTYPE     0x1A
#define REG_TXINTERVAL 0x1B
#define REG_RXTYPE     0x1C
#define REG_RXINTERVAL 0x1D
#define REG_DEVCTL     0x60
#define REG_TXFIFOSZ   0x62
#define REG_RXFIFOSZ   0x63
#define REG_TXFIFOADD  0x64
#define REG_RXFIFOADD  0x66
#define REG_HWVERS     0x6C
#define REG_RXTOG      0x80
#define REG_TXTOG      0x84
#define REG_TXFUNCADDR 0x480
#define REG_TXHUBADDR  0x482
#define REG_RXFUNCADDR 0x484
#define REG_RXHUBADDR  0x486
#define REG_OPSTATE    0x620
#define OPSTATE_HOST_WAIT_DEVICE 0x21
#define REG_FIFO(Ep)   (0x20 + 4 * (Ep))

#define POWER_SOFTCONN 0x40
#define POWER_HSENAB   0x20
#define POWER_HSMODE   0x10
#define POWER_RESET   0x08
#define POWER_RESUME  0x04
#define POWER_SUSPEND 0x02
#define USB_CONNECT   0x10
#define USB_DISCONNECT 0x20
#define USB_VBUS_ERROR 0x80
#define USB_BABBLE    0x04
#define DEVCTL_LSDEV  0x20
#define DEVCTL_FSDEV  0x40
#define DEVCTL_SESSION 0x01
#define DEVCTL_HOST_MODE 0x04

#define CSR0_RXREADY  0x01
#define CSR0_TXREADY  0x02
#define CSR0_STALL    0x04
#define CSR0_SETUP    0x08
#define CSR0_ERROR    0x10
#define CSR0_REQPKT   0x20
#define CSR0_STATUS   0x40
#define CSR0_NAK      0x80
#define CSR0_FLUSH    0x100
#define TX_READY      0x01
#define TX_ERROR      0x04
#define TX_FLUSH      0x08
#define TX_STALL      0x20
#define TX_NAK        0x80
#define TX_MODE       0x2000
#define RX_READY      0x01
#define RX_ERROR      0x04
#define RX_NAK        0x08
#define RX_FLUSH      0x10
#define RX_REQPKT     0x20
#define RX_STALL      0x40
#define RX_WZC_BITS   (RX_STALL | RX_ERROR | RX_NAK | RX_READY)
#define PROTO_BULK    0x20
#define PROTO_INTERRUPT 0x30
#define ASYNC_SLOTS   4
#define ASYNC_PACKET  64

STATIC inline UINT8 R8 (UINTN Reg) { return MmioRead8 (MUSB_BASE + Reg); }
STATIC inline UINT16 R16 (UINTN Reg) { return MmioRead16 (MUSB_BASE + Reg); }
STATIC inline VOID W8 (UINTN Reg, UINT8 V) { MmioWrite8 (MUSB_BASE + Reg, V); }
STATIC inline VOID W16 (UINTN Reg, UINT16 V) { MmioWrite16 (MUSB_BASE + Reg, V); }

// Board functions run serialized with controller accesses, only before EBS.
EFI_STATUS LancelotUsbBoardInit (VOID);
EFI_STATUS LancelotUsbPartnerIsSink (BOOLEAN *IsSink);
EFI_STATUS LancelotUsbGetRole (MTK_USB_ROLE *Role, UINT8 *Events);
EFI_STATUS LancelotUsbVbus (BOOLEAN Enable);
VOID LancelotUsbBoardExit (VOID);

typedef struct {
  BOOLEAN Used;
  BOOLEAN Pending;
  BOOLEAN Failed;
  UINT8 Address;
  UINT8 Endpoint;
  UINT8 Speed;
  UINT8 Toggle;
  UINTN MaxPacket;
  UINTN Length;
  UINT64 IntervalNs;
  UINT64 Due;
  EFI_ASYNC_USB_TRANSFER_CALLBACK Callback;
  VOID *Context;
} MUSB_ASYNC;

typedef struct {
  EFI_USB2_HC_PROTOCOL Protocol;
  EFI_USB_HC_STATE State;
  EFI_USB_PORT_STATUS Port;
  EFI_EVENT PollEvent;
  EFI_EVENT ScreenEvent;
  EFI_EVENT ExitEvent;
  BOOLEAN MacReady;
  BOOLEAN RoleAvailable;
  BOOLEAN HostAvailable;
  MTK_USB_ROLE ActiveRole;
  MTK_USB_ROLE DesiredRole;
  UINT64 RoleStableAt;
  MTK_USB_DEVICE_START DeviceStart;
  MTK_USB_DEVICE_STOP DeviceStop;
  VOID *DeviceContext;
  BOOLEAN Stopped;
  BOOLEAN Powered;
  BOOLEAN PowerOffPending;
  BOOLEAN WantPower;
  BOOLEAN Fault;
  BOOLEAN Partner;
  BOOLEAN DeviceRemoved;
  UINT8 LinkCandidate; // 0 unknown, 1 device present, 2 waiting for device
  UINT64 LinkDue;
  UINT64 LinkGuardUntil;
  UINT64 SessionRetryDue;
  UINT32 SessionRestarts;
  UINT32 PollCount;
  UINT8 LastDevCtl;
  UINT8 LastOpState;
  UINT8 LastPower;
  UINT8 LastBusEvents;
  UINT64 RoleDue;
  UINT64 ResetStarted;
  EFI_STATUS LastError;
  CONST CHAR8 *ErrorPhase;
  UINT16 VendorId;
  UINT16 ProductId;
  UINT8 DeviceAddress;
  UINT8 DeviceKind; // 0 unknown, 1 boot keyboard, 2 storage, 3 hub, 4 HID
  BOOLEAN Configured;
  BOOLEAN InputReceived;
  UINT8 KeyboardInterface;
  UINT32 HidReports;
  UINT32 KeyReports;
  UINT8 LastKey;
  UINT8 LedValue;
  UINT32 LedRequests;
  UINT32 LedCompleted;
  EFI_STATUS LedStatus;
  MUSB_ASYNC Async[ASYNC_SLOTS];
} MUSB_HOST;

#endif
