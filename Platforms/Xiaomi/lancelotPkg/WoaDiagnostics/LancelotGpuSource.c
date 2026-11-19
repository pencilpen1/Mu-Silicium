/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * A bounded same-boot source observation. No SMC, IPI, PMIC, clock mux, GPU
 * access or writer transition. It does not grant ATF/SSPM authority.
 * No ACPI namespace device or replacement GPU SSDT is added.
 */
#ifdef LGPU_FIRMWARE_HOST_TEST
#include "firmware_source_shim.h"
#else
#include <Uefi.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/PeImage.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/BootLogLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Protocol/LoadedImage.h>
#endif
#ifndef LGPU_UEFI
#define LGPU_UEFI
#endif
#include "lgpu_source.h"
#include "lgpu_firmware_build_identity.h"
#include "LancelotGpuSource.h"
extern VOID lgfs_sha256(CONST VOID *,UINT32,UINT8[32]);
STATIC struct lgpu_firmware_record mRecord;
STATIC struct lgpu_firmware_record *mLive;
STATIC UINTN mTableKey;
STATIC BOOLEAN mInitialized;
STATIC BOOLEAN mInstalled;
STATIC BOOLEAN mMeasured;
STATIC BOOLEAN mReadyCalled;
STATIC EFI_STATUS mStatus;
STATIC VOID Checksum(struct lgpu_firmware_record *Record)
{
    UINT8 Sum=0;UINTN I;Record->checksum=0;
    for(I=0;I<sizeof(*Record);I++)Sum=(UINT8)(Sum+((UINT8 *)Record)[I]);
    Record->checksum=(UINT8)(0u-Sum);MemoryFence();
}
STATIC BOOLEAN Measure(EFI_HANDLE ImageHandle,UINT8 Digest[32])
{
    EFI_LOADED_IMAGE_PROTOCOL *Loaded=NULL;
    EFI_STATUS Status=gBS->HandleProtocol(ImageHandle,&gEfiLoadedImageProtocolGuid,(VOID **)&Loaded);
    if(EFI_ERROR(Status) || !Loaded || !Loaded->ImageBase || Loaded->ImageSize<4096 || Loaded->ImageSize>0x400000u)return FALSE;
    CONST UINT8 *Base=Loaded->ImageBase;
    CONST EFI_IMAGE_DOS_HEADER *Dos=(CONST VOID *)Base;
    if(Dos->e_magic!=EFI_IMAGE_DOS_SIGNATURE || Dos->e_lfanew<(INT32)sizeof(*Dos) || Dos->e_lfanew>4096 ||
       (UINT32)Dos->e_lfanew>Loaded->ImageSize-sizeof(EFI_IMAGE_NT_HEADERS64))return FALSE;
    CONST EFI_IMAGE_NT_HEADERS64 *Nt=(CONST VOID *)(Base+Dos->e_lfanew);
    if(Nt->Signature!=EFI_IMAGE_NT_SIGNATURE || Nt->OptionalHeader.Magic!=EFI_IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
       Nt->FileHeader.Machine!=0xAA64u || Nt->FileHeader.SizeOfOptionalHeader!=sizeof(EFI_IMAGE_OPTIONAL_HEADER64) ||
       Nt->FileHeader.NumberOfSections>32 || Nt->OptionalHeader.SizeOfImage>Loaded->ImageSize)return FALSE;
    CONST EFI_IMAGE_SECTION_HEADER *Sections=(CONST VOID *)((CONST UINT8 *)&Nt->OptionalHeader+Nt->FileHeader.SizeOfOptionalHeader);
    if((UINTN)((CONST UINT8 *)Sections-Base)>Loaded->ImageSize-
       Nt->FileHeader.NumberOfSections*sizeof(*Sections))return FALSE;
    UINTN I;
    for(I=0;I<Nt->FileHeader.NumberOfSections;I++) {
        CONST EFI_IMAGE_SECTION_HEADER *Section=&Sections[I];
        UINT32 Offset=Section->VirtualAddress,Size=Section->Misc.VirtualSize;
        if(CompareMem(Section->Name,".text\0\0\0",8)!=0)continue;
        if(!Size || Size>Loaded->ImageSize || Offset>Loaded->ImageSize-Size ||
           !(Section->Characteristics&EFI_IMAGE_SCN_MEM_EXECUTE) || (Section->Characteristics&EFI_IMAGE_SCN_MEM_WRITE))return FALSE;
        lgfs_sha256(Base+Offset,Size,Digest);return TRUE;
    }
    return FALSE;
}
STATIC VOID AttachRecord(VOID)
{
    EFI_ACPI_TABLE_PROTOCOL *Acpi=NULL;EFI_ACPI_SDT_PROTOCOL *Sdt=NULL;
    UINTN I;
    if(!mInstalled) {
        mStatus=gBS->LocateProtocol(&gEfiAcpiTableProtocolGuid,NULL,(VOID **)&Acpi);
        if(EFI_ERROR(mStatus))return;
        mStatus=Acpi->InstallAcpiTable(Acpi,&mRecord,sizeof(mRecord),&mTableKey);
        if(EFI_ERROR(mStatus))return;
        mInstalled=TRUE;
    }
    mStatus=gBS->LocateProtocol(&gEfiAcpiSdtProtocolGuid,NULL,(VOID **)&Sdt);
    if(EFI_ERROR(mStatus))return;
    for(I=0;I<128;I++) {
        EFI_ACPI_SDT_HEADER *Header=NULL;EFI_ACPI_TABLE_VERSION Version;UINTN Key;
        mStatus=Sdt->GetAcpiTable(I,&Header,&Version,&Key);
        if(EFI_ERROR(mStatus))break;
        if(Key==mTableKey && Header && Header->Signature==LGFS_SIGNATURE && Header->Length==sizeof(mRecord)) {
            mLive=(VOID *)Header;CopyMem(mLive,&mRecord,sizeof(mRecord));mStatus=EFI_SUCCESS;return;
        }
    }
    if(!EFI_ERROR(mStatus))mStatus=EFI_NOT_FOUND;
}
VOID LancelotGpuSourceInitialize(EFI_HANDLE ImageHandle,UINT64 BootId)
{
    if(mInitialized)return;mInitialized=TRUE;
    ZeroMem(&mRecord,sizeof(mRecord));mRecord.signature=LGFS_SIGNATURE;mRecord.length=sizeof(mRecord);
    mRecord.acpi_revision=1;CopyMem(mRecord.oem_id,"LNCLT ",6);CopyMem(mRecord.oem_table_id,"GPUSRC01",8);
    mRecord.oem_revision=1;mRecord.creator_id=0x5550474cu;mRecord.creator_revision=1;
    mRecord.version=LGFS_VERSION;mRecord.board_id=LGFS_BOARD;mRecord.payload_bytes=sizeof(mRecord)-36u;
    mRecord.phase=LGFS_DXE;mRecord.boot_id=BootId;mRecord.record_generation=1;
    CopyMem(mRecord.baseline_commit,"b077948552b749b74451acc72bd00b4b679bc565",40);
    CopyMem(mRecord.source_sha256,LGPU_FIRMWARE_SOURCE_SHA256,32);
    if(!BootId || !Measure(ImageHandle,mRecord.image_sha256)){mStatus=EFI_NOT_READY;goto Done;}
    mMeasured=TRUE;
    Checksum(&mRecord);
    AttachRecord();
Done:
    BootLogPrint("[WOA] GPU_SOURCE phase=DXE status=0x%lx table_installed=%u live_record=%u boot_id=%Lu writer_observed=0 writer_authorized=0 hardware_reads=0 hardware_writes=0\n",
        (UINT64)mStatus,mInstalled,mLive!=NULL,BootId);
}
VOID LancelotGpuSourceReady(VOID)
{
    if(mReadyCalled)return;mReadyCalled=TRUE;
    if(mMeasured) {
        struct lgpu_firmware_record *Record=mLive?mLive:&mRecord;
        Record->phase=LGFS_READY;Record->record_generation++;
        Record->counter_frequency=GetPerformanceCounterProperties(NULL,NULL);
        Record->ready_counter=GetPerformanceCounter();Checksum(Record);
        /* DXE diagnostics intentionally keeps Depex TRUE so existing owner
         * timing is unchanged. ACPI protocols may appear later; one READY
         * metadata-only install/attach is bounded and never performed at EBS. */
        if(!mLive)AttachRecord();
        if(mLive)WriteBackDataCacheRange(mLive,sizeof(*mLive));
    }
    BootLogPrint("[WOA] GPU_SOURCE phase=READY live_record=%u writer_observed=0 writer_authorized=0 no_runtime_grant=1\n",mLive!=NULL);
}
VOID LancelotGpuSourceExit(VOID)
{
    /* Only update the already installed record. No allocation, protocol,
     * timer, table installation, writer handoff or hardware operation at EBS. */
    if(mLive) {
        mLive->phase=LGFS_EBS;mLive->record_generation++;Checksum(mLive);
        WriteBackDataCacheRange(mLive,sizeof(*mLive));
    }
    BootLogPrint("[WOA] GPU_SOURCE phase=EBS live_record=%u writer_observed=0 writer_authorized=0 no_runtime_grant=1\n",mLive!=NULL);
}
