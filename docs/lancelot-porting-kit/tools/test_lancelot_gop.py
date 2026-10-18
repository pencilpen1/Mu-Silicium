#!/usr/bin/env python3
"""Host-check actual GOP BLT adapter + library, without display hardware."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
driver = (ROOT / "ws/MT6768Pkg/Drivers/SimpleFbDxe/SimpleFbDxe.c").read_text()
start = driver.index("STATIC\nEFI_STATUS\nEFIAPI\nDisplayBlt(", driver.index("DisplaySetMode(IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This, IN UINT32 ModeNumber)\n{"))
adapter = driver[start:driver.index("EFI_STATUS\nEFIAPI\nSimpleFbDxeInitialize", start)]
source = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#undef NULL
#include <Uefi.h>
#include <Library/CacheMaintenanceLib.h>
#include "ws/MT6768Pkg/Library/FrameBufferBltLib/FrameBufferBltLib.c"
static FRAME_BUFFER_CONFIGURE *mFrameBufferBltLibConfigure;
static EFI_BOOT_SERVICES BootServices;
static EFI_BOOT_SERVICES *gBS = &BootServices;
static UINTN Flushes;
static VOID *FlushStart;
static UINTN FlushLength;
static EFI_TPL CurrentTpl = TPL_APPLICATION;
VOID *EFIAPI CopyMem(VOID *D, CONST VOID *S, UINTN N) { return memmove(D,S,N); }
VOID *EFIAPI SetMem(VOID *D, UINTN N, UINT8 V) { return memset(D,V,N); }
VOID *EFIAPI SetMem32(VOID *D, UINTN N, UINT32 V) {
  for(UINTN I=0;I<N;I+=4) memcpy((char *)D+I,&V,4);
  return D;
}
VOID *EFIAPI SetMem64(VOID *D, UINTN N, UINT64 V) {
  for(UINTN I=0;I<N;I+=8) memcpy((char *)D+I,&V,8);
  return D;
}
INTN EFIAPI HighBitSet32(UINT32 V) { return V ? 31-__builtin_clz(V) : -1; }
BOOLEAN EFIAPI DebugAssertEnabled(VOID) { return TRUE; }
VOID EFIAPI DebugAssert(CONST CHAR8 *F, UINTN L, CONST CHAR8 *D) {
  fprintf(stderr,"%s:%lu: %s\n",F,(unsigned long)L,D); abort();
}
BOOLEAN EFIAPI DebugPrintEnabled(VOID) { return FALSE; }
BOOLEAN EFIAPI DebugPrintLevelEnabled(CONST UINTN L) { return FALSE; }
VOID EFIAPI DebugPrint(UINTN L, CONST CHAR8 *F, ...) {}
VOID *EFIAPI WriteBackDataCacheRange(VOID *P, UINTN N) {
  assert(CurrentTpl==TPL_NOTIFY); Flushes++; FlushStart=P; FlushLength=N; return P;
}
static EFI_TPL EFIAPI Raise(EFI_TPL T) { EFI_TPL Old=CurrentTpl; CurrentTpl=T; return Old; }
static VOID EFIAPI Restore(EFI_TPL T) { CurrentTpl=T; }
'''
tests = r'''
int main(void) {
  enum { W=8, H=6, STRIDE=12 };
  UINT32 Fb[H*STRIDE], Saved[H*STRIDE];
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info={0};
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode={0};
  EFI_GRAPHICS_OUTPUT_PROTOCOL Gop={0};
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL In[W*H], Out[W*H];
  BootServices.RaiseTPL=Raise; BootServices.RestoreTPL=Restore;
  Info.HorizontalResolution=W; Info.VerticalResolution=H; Info.PixelsPerScanLine=STRIDE;
  Mode.Info=&Info; Mode.FrameBufferBase=(UINTN)Fb; Mode.FrameBufferSize=sizeof(Fb);
  Gop.Mode=&Mode;
  for (int Format=0;Format<2;Format++) {
    Info.PixelFormat=Format;
    UINTN Size=0;
    assert(FrameBufferBltConfigure(Fb,&Info,NULL,&Size)==RETURN_BUFFER_TOO_SMALL);
    mFrameBufferBltLibConfigure=malloc(Size);
    assert(FrameBufferBltConfigure(Fb,&Info,mFrameBufferBltLibConfigure,&Size)==0);
    for(int I=0;I<W*H;I++) In[I]=(EFI_GRAPHICS_OUTPUT_BLT_PIXEL){I+1, I+2, I+3, 0};
    for(int I=0;I<H*STRIDE;I++) Fb[I]=0x12345678;
    assert(DisplayBlt(&Gop,In,EfiBltBufferToVideo,0,0,0,0,W,H,0)==0);
    for(int Y=0;Y<H;Y++) for(int X=0;X<STRIDE;X++)
      assert(X<W ? (Fb[Y*STRIDE+X]>>24)==255 : Fb[Y*STRIDE+X]==0x12345678);
    UINTN Count=Flushes;
    assert(DisplayBlt(&Gop,Out,EfiBltVideoToBltBuffer,0,0,0,0,W,H,0)==0);
    assert(Flushes==Count);
    for(int I=0;I<W*H;I++) assert(memcmp(&In[I],&Out[I],3)==0);
    memcpy(Saved,Fb,sizeof(Fb));
    // Downward overlapping copy reaching the last framebuffer row.
    assert(DisplayBlt(&Gop,NULL,EfiBltVideoToVideo,0,0,0,1,W,H-1,0)==0);
    for(int Y=1;Y<H;Y++) assert(memcmp(Fb+Y*STRIDE,Saved+(Y-1)*STRIDE,W*4)==0);
    assert(FlushStart==Fb+STRIDE && FlushLength==(H-1)*STRIDE*4);
    memcpy(Saved,Fb,sizeof(Fb));
    assert(DisplayBlt(&Gop,NULL,EfiBltVideoToVideo,0,1,0,0,W,H-1,0)==0);
    for(int Y=0;Y<H-1;Y++) assert(memcmp(Fb+Y*STRIDE,Saved+(Y+1)*STRIDE,W*4)==0);
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL White={255,255,255,0};
    assert(DisplayBlt(&Gop,&White,EfiBltVideoFill,0,0,2,3,3,1,0)==0);
    for(int X=2;X<5;X++) assert(Fb[3*STRIDE+X]==0xffffffff);
    Count=Flushes; memcpy(Saved,Fb,sizeof(Fb));
    assert(EFI_ERROR(DisplayBlt(&Gop,&White,EfiBltVideoFill,0,0,~(UINTN)0,0,2,1,0)));
    assert(EFI_ERROR(DisplayBlt(&Gop,NULL,EfiBltBufferToVideo,0,0,0,0,W,H,0)));
    assert(Flushes==Count && memcmp(Saved,Fb,sizeof(Fb))==0);
    assert(CurrentTpl==TPL_APPLICATION);
    free(mFrameBufferBltLibConfigure);
  }
  puts("PASS: GOP RGB/BGR conversion, opaque pixels, stride, overlap scroll, dirty flush and invalid rectangle (ASan/UBSan)");
}
'''
with tempfile.TemporaryDirectory(prefix="gop-test-", dir="/tmp/opencode") as temp:
    temp = Path(temp)
    cfile = temp / "test.c"
    cfile.write_text(source + adapter + tests)
    exe = temp / "test"
    includes = [ROOT, ROOT / "ws/edk2/MdePkg/Include", ROOT / "ws/edk2/MdePkg/Include/AArch64",
                ROOT / "ws/edk2/MdeModulePkg/Include"]
    subprocess.run(["gcc", "-std=c11", "-g", "-O1", "-fshort-wchar", "-fno-strict-aliasing",
                    "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie",
                    *("-I" + str(p) for p in includes), str(cfile), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=30)
