/* Exercise the actual incremental overlay against guarded, patterned RAM.
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#undef NULL
#include <Uefi.h>

static UINT32 Pixels[2342][1088];
#define USB_STATUS_ADDRESS ((UINTN)&Pixels[1][0])
#include "Silicon/MediaTek/MediaTekPkg/Drivers/MusbHostDxe/UsbStatus.c"

static BOOLEAN FailAllocation;
static UINTN Flushes;
VOID *EFIAPI AllocateZeroPool(UINTN Size) {
  return FailAllocation ? NULL : calloc(1,Size);
}
VOID *EFIAPI WriteBackDataCacheRange(VOID *Address,UINTN Size) {
  UINTN Offset=((UINTN)Address-USB_STATUS_ADDRESS)/sizeof(UINT32);
  UINTN Row=Offset/STATUS_STRIDE, Column=Offset%STATUS_STRIDE;
  assert(Row>=STATUS_TOP && Row<STATUS_TOP+STATUS_HEIGHT);
  assert(Size>0 && Column+Size/sizeof(UINT32)<=STATUS_WIDTH);
  Flushes++; return Address;
}

int main(void) {
  UINT32 *Original=malloc(sizeof(Pixels));
  assert(Original);
  for(UINTN I=0;I<sizeof(Pixels)/sizeof(UINT32);I++)
    ((UINT32*)Pixels)[I]=0xFF000000U | (UINT32)((I*997)&0xFFFFFF);
  memcpy(Original,Pixels,sizeof(Pixels));
  FailAllocation=TRUE; UsbStatusInitialize(); UsbStatusUpdate("USB HOST","OTG");
  assert(mSurface==NULL && Flushes==0 && !memcmp(Original,Pixels,sizeof(Pixels)));
  FailAllocation=FALSE; UsbStatusInitialize(); assert(mSurface);
  UsbStatusUpdate("USB HOST: WAITING FOR OTG","C=00 I=00 T=1");
  assert(Flushes>0 && memcmp(Original,Pixels,sizeof(Pixels)));
  UINTN Old=Flushes;
  UsbStatusUpdate("USB HOST: WAITING FOR OTG","C=00 I=00 T=1");
  assert(Flushes==Old); // identical state does not write back any pixels
  for(UINTN Y=0;Y<2342;Y++) for(UINTN X=0;X<1088;X++) {
    if(Y<STATUS_TOP+1 || Y>=STATUS_TOP+1+STATUS_HEIGHT || X>=STATUS_WIDTH)
      assert(Pixels[Y][X]==Original[Y*1088+X]);
  }
  UsbStatusUpdate("USB DEVICE: FASTBOOT ACTIVE","18d1:d00d / lancelot-uefi");
  UsbStatusUpdate("U","");
  for(UINTN Y=STATUS_TOP+1;Y<STATUS_TOP+1+STATUS_HEIGHT;Y++)
    for(UINTN X=STATUS_CELL_W;X<1088;X++) assert(Pixels[Y][X]==Original[Y*1088+X]);
  UsbStatusUpdate("",NULL);
  assert(!memcmp(Original,Pixels,sizeof(Pixels))); // erase restores the exact UI, not a flat blue strip
  UsbStatusUpdate("USB HOST","TEST");
  // GOP paints a new panel underneath. The next overlay must adopt the new
  // background, not revive the screenshot from before the panel redraw.
  for(UINTN Y=STATUS_TOP+1;Y<STATUS_TOP+1+STATUS_HEIGHT;Y++)
    for(UINTN X=0;X<STATUS_WIDTH;X++) Pixels[Y][X]=0xFF123456;
  UsbStatusUpdate("USB HOST","TEST");
  Old=Flushes; UsbStatusUpdate("USB HOST","TEST"); assert(Flushes==Old);
  UsbStatusUpdate("","");
  for(UINTN Y=STATUS_TOP+1;Y<STATUS_TOP+1+STATUS_HEIGHT;Y++)
    for(UINTN X=0;X<STATUS_WIDTH;X++) assert(Pixels[Y][X]==0xFF123456);
  UsbStatusUpdate("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789","\001\377");
  UsbStatusUpdate(NULL,NULL); // clipping and non-ASCII fallback stay within bounds
  free(mSurface); free(Original);
  puts("PASS: status delta pixels, unchanged no-flush, background restore, GOP redraw adoption, clipping and stride guards (ASan/UBSan)");
  return 0;
}
