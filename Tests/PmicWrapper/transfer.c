#include "host.h"
#include <Library/PmicWrapperImplLib.h>
#include <Protocol/MtkPmicWrapper.h>
MTK_PMIC_WRAPPER_PLATFORM_INFO gPlatformInfo;
EFI_GUID gMediaTekPmicWrapperProtocolGuid;
EFI_GUID gMediaTekPmicWrapperCheckedProtocolGuid;
#include MU_PWRAP_SOURCE

static UINT32 regs[1024];
static UINT16 pmic[32768];
static UINT64 now, jump_time;
static unsigned ops, writes, locks, releases, installed, jump_op;
static BOOLEAN hang, frozen, sticky, write_busy, reenter;
static EFI_STATUS nested;
static EFI_STATUS install(EFI_HANDLE *handle,...) {
    (void)handle;va_list ap;va_start(ap,handle);
    assert(va_arg(ap,EFI_GUID*)==&gMediaTekPmicWrapperProtocolGuid);
    assert(va_arg(ap,MTK_PMIC_WRAPPER_PROTOCOL*)==&mPmicWrapper);
    assert(va_arg(ap,EFI_GUID*)==&gMediaTekPmicWrapperCheckedProtocolGuid);
    assert(va_arg(ap,MTK_PMIC_WRAPPER_PROTOCOL*)==&mPmicWrapper);
    assert(va_arg(ap,void*)==NULL);va_end(ap);
    installed++;return EFI_SUCCESS;
}
static EFI_BOOT_SERVICES services={install,NULL};
EFI_BOOT_SERVICES *gBS=&services;
EFI_STATUS EfiAcquireLockOrFail(EFI_LOCK *l) {
    if(l->held)return EFI_ACCESS_DENIED;
    l->held=true;locks++;return EFI_SUCCESS;
}
VOID EfiReleaseLock(EFI_LOCK *l) {assert(l->held);l->held=false;releases++;}
UINT64 GetPerformanceCounter(void) {return now;}
UINT64 GetTimeInNanoSecond(UINT64 v) {return v;}
VOID MicroSecondDelay(UINTN us) {assert(us==1);if(!frozen)now+=us*1000;}
EFI_STATUS LocateMemoryRegionByName(CONST CHAR8 *n,EFI_MEMORY_REGION_DESCRIPTOR *r) {
    assert(!strcmp(n,"PMIC Wrapper"));r->Address=0;r->Length=4096;return EFI_SUCCESS;
}
static void step(void) {ops++;if(ops==jump_op)now=jump_time;}
UINT32 MmioRead32(UINTN off) {
    assert(off<4096);step();
    if(off!=0x94)assert(mTransactionLock.held);
    if(reenter) {UINT16 v=0xdead;reenter=false;unsigned before=ops;
        nested=mPmicWrapper.Read(0xa,&v);assert(v==0xdead&&ops==before);}
    return regs[off/4];
}
VOID MmioWrite32(UINTN off,UINT32 value) {
    assert(mTransactionLock.held&&off<4096);step();writes++;
    if(off==0xc20) {
        UINT16 adr=(value>>16)&0x7fff;
        if(hang) {regs[0xc24/4]=WACS_INIT_DONE|WACS_REQ;return;}
        if(value>>31) {
            pmic[adr]=(UINT16)value;
            regs[0xc24/4]=WACS_INIT_DONE|WACS_SYNC_IDLE|(write_busy?WACS_REQ:0);
        } else regs[0xc24/4]=WACS_INIT_DONE|WACS_SYNC_IDLE|(6u<<16)|pmic[adr];
    } else if(off==0xc28) {
        assert(value==1);if(!sticky)regs[0xc24/4]=WACS_INIT_DONE|WACS_SYNC_IDLE;
    } else assert(!"unallowlisted controller write");
}
static void reset(void) {
    memset(regs,0,sizeof(regs));memset(pmic,0,sizeof(pmic));
    gPlatformInfo=(MTK_PMIC_WRAPPER_PLATFORM_INFO){0};
    gPlatformInfo.RegMap[PmicWrapperInitDone2]=0x94;
    gPlatformInfo.RegMap[PmicWrapperWacs2Cmd]=0xc20;
    gPlatformInfo.RegMap[PmicWrapperWacs2RData]=0xc24;
    gPlatformInfo.RegMap[PmicWrapperWacs2VldClr]=0xc28;
    regs[0x94/4]=1;regs[0xc24/4]=WACS_INIT_DONE|WACS_SYNC_IDLE;pmic[5]=0x5830;
    now=0;ops=writes=locks=releases=installed=jump_op=0;
    hang=frozen=sticky=write_busy=reenter=mPoisoned=mTransactionLock.held=false;
}
static void quarantined(EFI_STATUS want) {
    UINT16 v=0xdead;assert(mPmicWrapper.Read(0xa,&v)==want);
    assert(v==0xdead&&mPoisoned&&locks==1&&releases==1&&!mTransactionLock.held);
    unsigned old=ops;assert(mPmicWrapper.Read(0xa,&v)==EFI_DEVICE_ERROR&&ops==old);
    assert(mPmicWrapper.Write(0x1508,1)==EFI_DEVICE_ERROR&&ops==old);
}
int main(void) {
    UINT16 v=0xdead;
    reset();regs[0x94/4]=0;assert(InitPmicWrapper(NULL,NULL)==EFI_NOT_READY&&!installed);
    reset();assert(!InitPmicWrapper(NULL,NULL)&&installed==1);ops=0;
    assert(!mPmicWrapper.Read(0xa,&v)&&v==0x5830&&writes==2&&!mPoisoned);
    unsigned success_ops=ops;assert(success_ops==5);
    reset();assert(mPmicWrapper.Read(0xb,&v)==EFI_INVALID_PARAMETER&&!ops);
    assert(mPmicWrapper.Read(0xa,NULL)==EFI_INVALID_PARAMETER&&!ops);
    reset();regs[0xc24/4]|=WACS_REQ;assert(mPmicWrapper.Read(0xa,&v)==EFI_NOT_READY&&!writes&&!mPoisoned);
    reset();regs[0xc24/4]|=6u<<16;assert(mPmicWrapper.Read(0xa,&v)==EFI_NOT_READY&&!writes&&!mPoisoned);
    reset();regs[0xc24/4]=WACS_INIT_DONE;
    assert(mPmicWrapper.Read(0xa,&v)==EFI_TIMEOUT&&!writes&&!mPoisoned&&ops<=PWRAP_MAX_POLLS);
    reset();hang=true;quarantined(EFI_TIMEOUT);assert(ops<=PWRAP_MAX_POLLS+2);
    reset();hang=frozen=true;quarantined(EFI_TIMEOUT);assert(ops==PWRAP_MAX_POLLS+2);
    reset();sticky=true;quarantined(EFI_TIMEOUT);
    for(unsigned i=2;i<=success_ops;i++) {
        reset();jump_op=i;jump_time=PWRAP_TIMEOUT_NS;quarantined(EFI_TIMEOUT);
        reset();now=100;jump_op=i;jump_time=99;quarantined(EFI_TIMEOUT);
    }
    reset();jump_op=1;jump_time=PWRAP_TIMEOUT_NS;v=0xdead;
    assert(mPmicWrapper.Read(0xa,&v)==EFI_TIMEOUT&&!writes&&!mPoisoned&&v==0xdead);
    reset();reenter=true;assert(!mPmicWrapper.Read(0xa,&v)&&nested==EFI_ACCESS_DENIED);
    reset();pmic[0x1508/2]=0xa5a0;
    assert(!mPmicWrapper.UpdateBits(0x1508,1,1)&&pmic[0x1508/2]==0xa5a1);
    assert(locks==1&&releases==1&&writes==3);
    unsigned old=ops;assert(mPmicWrapper.UpdateBits(0x1508,1,2)==EFI_INVALID_PARAMETER&&ops==old);
    assert(!mPmicWrapper.UpdateBits(0x1508,1,1)&&writes==5); // no redundant PMIC write
    reset();write_busy=true;assert(mPmicWrapper.Write(0x1508,1)==EFI_TIMEOUT&&mPoisoned);
    puts("Actual Mu PWRAP: status/output, initial foreign REQ/ACK, deadline/stalled timer/rollback, final completion, quarantine, lock reentry and atomic RMW PASS; RAM ONLY");
    return 0;
}
