/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <Uefi.h>
#include <Library/BootLogLib.h>
#include <Library/SerialPortLib.h>

RETURN_STATUS EFIAPI SerialPortInitialize (VOID) { return RETURN_SUCCESS; }
UINTN EFIAPI SerialPortWrite (UINT8 *Buffer, UINTN NumberOfBytes)
{
  BootLogWrite (Buffer, NumberOfBytes);
  return NumberOfBytes;
}
UINTN EFIAPI SerialPortRead (UINT8 *Buffer, UINTN NumberOfBytes) { return 0; }
BOOLEAN EFIAPI SerialPortPoll (VOID) { return FALSE; }
RETURN_STATUS EFIAPI SerialPortSetControl (UINT32 Control) { return RETURN_UNSUPPORTED; }
RETURN_STATUS EFIAPI SerialPortGetControl (UINT32 *Control)
{
  if (Control == NULL) return RETURN_INVALID_PARAMETER;
  *Control = EFI_SERIAL_INPUT_BUFFER_EMPTY | EFI_SERIAL_OUTPUT_BUFFER_EMPTY;
  return RETURN_SUCCESS;
}
RETURN_STATUS EFIAPI SerialPortSetAttributes (
  UINT64 *BaudRate, UINT32 *ReceiveFifoDepth, UINT32 *Timeout,
  EFI_PARITY_TYPE *Parity, UINT8 *DataBits, EFI_STOP_BITS_TYPE *StopBits)
{
  return RETURN_UNSUPPORTED;
}
