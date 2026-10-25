#include "host.h"
#include <Protocol/MtkPmicWrapper.h>
#include <Protocol/MtkPmic.h>
EFI_GUID gMediaTekPmicWrapperProtocolGuid,gMediaTekPmicProtocolGuid;
#include MU_PMIC_SOURCE
static EFI_STATUS failure;
static UINT16 raw,adr,mask,bits;
static unsigned reads,updates;
static EFI_STATUS read_value(UINT16 a,UINT16 *v) {
    reads++;adr=a;if(EFI_ERROR(failure))return failure;*v=raw;return EFI_SUCCESS;
}
static EFI_STATUS write_value(UINT16 a,UINT16 v) {(void)a;(void)v;assert(false);return EFI_DEVICE_ERROR;}
static EFI_STATUS update(UINT16 a,UINT16 m,UINT16 b) {updates++;adr=a;mask=m;bits=b;return failure;}
static MTK_PMIC_WRAPPER_PROTOCOL wrapper={read_value,write_value,update};
static EFI_STATUS install(EFI_HANDLE *handle,...) {(void)handle;return EFI_SUCCESS;}
static EFI_STATUS locate(EFI_GUID *guid,VOID *reg,VOID **out) {(void)guid;(void)reg;*out=&wrapper;return EFI_SUCCESS;}
static EFI_BOOT_SERVICES services={install,locate};
EFI_BOOT_SERVICES *gBS=&services;
int main(void) {
    assert(!InitPmic(NULL,NULL));BOOLEAN enabled=true,pressed=true;UINT32 uv=0xdead;
    failure=EFI_TIMEOUT;
    assert(RegulatorIsEnabled("buck_vgpu",&enabled)==failure&&enabled);
    assert(RegulatorGetVoltage("buck_vgpu",&uv)==failure&&uv==0xdead);
    assert(RegulatorSetEnable("buck_vgpu",true)==failure&&updates==1&&adr==0x1508&&mask==1&&bits==1);
    assert(RegulatorSetVoltage("buck_vgpu",950000)==failure&&updates==2&&adr==0x1526&&bits==72);
    PowerButtonPressed(&pressed);assert(!pressed);pressed=true;
    HomeButtonPressed(&pressed);assert(!pressed);
    failure=EFI_SUCCESS;raw=1;
    assert(!RegulatorIsEnabled("buck_vgpu",&enabled)&&enabled);
    raw=72;assert(!RegulatorGetVoltage("buck_vgpu",&uv)&&uv==950000);
    assert(!RegulatorSetVoltage("ldo_vusb",3000000)&&adr==0x1e34&&mask==0x700&&bits==0x300);
    assert(RegulatorSetVoltage("ldo_vusb",3000001)==EFI_INVALID_PARAMETER);
    assert(RegulatorIsEnabled("buck_vgpu",NULL)==EFI_INVALID_PARAMETER);
    assert(RegulatorGetVoltage("buck_vgpu",NULL)==EFI_INVALID_PARAMETER);
    assert(RegulatorSetEnable("unknown",true)==EFI_INVALID_PARAMETER);
    puts("Actual MT6358 consumers: errors propagated, failed reads leave outputs unchanged, atomic enable/voltage fields and LDO selector mask PASS; RAM ONLY");
    return 0;
}
