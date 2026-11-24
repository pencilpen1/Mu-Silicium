/* SPDX-License-Identifier: MIT -- main-owned UEFI integration proposal only.
 * Include after the existing Log() definition in LancelotPreflight.c.
 * Default0 keeps ordinary startup unchanged. Main explicitly builds1/2/3.
 */
#include <Protocol/AcpiSystemDescriptionTable.h>
#include "lgpu_bootstrap.h"
#include "lgpu_native_job.h"
#ifndef LANCELOT_GPU_BOOTSTRAP_MODE
#define LANCELOT_GPU_BOOTSTRAP_MODE 0
#endif
#if LANCELOT_GPU_BOOTSTRAP_MODE < 0 || LANCELOT_GPU_BOOTSTRAP_MODE > 3
#error LANCELOT_GPU_BOOTSTRAP_MODE must be0,1,2,or3
#endif
#if LANCELOT_GPU_BOOTSTRAP_MODE == 3
/* Failure evidence cannot depend on Windows starting or file I/O. Export
 * complete copied bytes into the original fixed RAM logger, with a CRC.
 * No hardware read, new logger initialization or resource action here. */
STATIC VOID GpuJobRawReceipt(CONST struct lgj_preboot_record *R)
{
  CONST UINT8 *Bytes=(CONST UINT8 *)R;UINT32 Crc=~0U;
  for(UINTN I=0;I<sizeof(*R);++I){Crc^=Bytes[I];for(UINTN B=0;B<8;++B)Crc=(Crc>>1)^((0U-(Crc&1U))&0xEDB88320U);}
  Crc=~Crc;CHAR8 Line[224];UINTN N;
  N=AsciiSPrint(Line,sizeof(Line),"LGBJ_RAW_BEGIN boot=%016lx bytes=%u crc32=%08x\r\n",R->boot_id,(UINT32)sizeof(*R),Crc);
  BootLogWrite(Line,N);CONST CHAR8 Hex[]="0123456789abcdef";
  for(UINTN At=0;At<sizeof(*R);At+=64){
    CHAR8 Data[129];UINTN Count=sizeof(*R)-At;if(Count>64)Count=64;
    for(UINTN J=0;J<Count;++J){Data[2*J]=Hex[Bytes[At+J]>>4];Data[2*J+1]=Hex[Bytes[At+J]&15];}Data[2*Count]=0;
    N=AsciiSPrint(Line,sizeof(Line),"LGBJ_RAW_CHUNK offset=%u data=%a\r\n",(UINT32)At,Data);BootLogWrite(Line,N);
  }
  N=AsciiSPrint(Line,sizeof(Line),"LGBJ_RAW_END boot=%016lx bytes=%u crc32=%08x complete=1 runtime_grant=0\r\n",R->boot_id,(UINT32)sizeof(*R),Crc);
  BootLogWrite(Line,N);
}
#endif
STATIC EFI_STATUS RunGpuBootstrapBeforeWindows(EFI_HANDLE Parent,EFI_HANDLE Device)
{
#if LANCELOT_GPU_BOOTSTRAP_MODE == 0
  (VOID)Parent;(VOID)Device;return EFI_SUCCESS;
#else
  STATIC EFI_GUID SdtGuid={0xeb97088e,0xcfdf,0x49c6,{0xbe,0x4b,0xd9,0x06,0xa5,0xb2,0x0e,0x86}};
#if LANCELOT_GPU_BOOTSTRAP_MODE == 1
  STATIC CHAR16 Options[]=L"--identity-once";
#elif LANCELOT_GPU_BOOTSTRAP_MODE == 2
  STATIC CHAR16 Options[]=L"--power-id-once";
#else
  STATIC CHAR16 Options[]=L"--power-job-once";
#endif
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_HANDLE Child=NULL;EFI_LOADED_IMAGE_PROTOCOL *Loaded=NULL;
  EFI_ACPI_SDT_PROTOCOL *Sdt=NULL;CONST struct lgbs_record *Receipt=NULL;
#if LANCELOT_GPU_BOOTSTRAP_MODE == 3
  CONST struct lgj_preboot_record *Job=NULL;UINT32 JobCount=0;
#endif
  EFI_STATUS Status;UINT32 Count=0;BOOLEAN Finished=FALSE;
  if(mHandoff->Magic!=LANCELOT_HANDOFF_MAGIC || !mHandoff->BootId ||
     mHandoff->Stage!=LANCELOT_STAGE_PREFLIGHT)return EFI_NOT_READY;
  Log("GPU_PREBOOT_BEGIN mode=%u boot=%016lx physical_request=1 runtime_grant=0 before_windows=1\r\n",
      LANCELOT_GPU_BOOTSTRAP_MODE,mHandoff->BootId);
  Path=FileDevicePath(Device,L"\\EFI\\WOA\\LancelotGpuBootstrap.efi");
  if(!Path)return EFI_OUT_OF_RESOURCES;
  Status=gBS->LoadImage(FALSE,Parent,Path,NULL,0,&Child);FreePool(Path);
  Log("GPU_PREBOOT_LOAD status=%r\r\n",Status);if(Status!=EFI_SUCCESS)return Status;
  Status=gBS->HandleProtocol(Child,&gEfiLoadedImageProtocolGuid,(VOID **)&Loaded);
  if(Status==EFI_SUCCESS && Loaded) {
    Loaded->LoadOptions=Options;Loaded->LoadOptionsSize=sizeof(Options);
    Status=gBS->StartImage(Child,NULL,NULL); /* one invocation; no retry */
  }
  EFI_STATUS ChildStatus=Status;
  Log("GPU_PREBOOT_RETURN status=%r\r\n",ChildStatus);
  Status=gBS->LocateProtocol(&SdtGuid,NULL,(VOID **)&Sdt);
  if(Status==EFI_SUCCESS && Sdt)for(UINTN I=0;I<128;++I) {
    EFI_ACPI_SDT_HEADER *Header=NULL;EFI_ACPI_TABLE_VERSION Version;UINTN Key;
    Status=Sdt->GetAcpiTable(I,&Header,&Version,&Key);
    if(Status==EFI_NOT_FOUND){Finished=TRUE;break;}if(Status!=EFI_SUCCESS)break;
    if(Header && Header->Signature==LGBS_SIGNATURE) {
      ++Count;if(Header->Length==sizeof(struct lgbs_record))Receipt=(CONST VOID *)Header;
    }
#if LANCELOT_GPU_BOOTSTRAP_MODE == 3
    if(Header && Header->Signature==LGJ_ACPI_SIGNATURE) {
      ++JobCount;if(Header->Length==sizeof(struct lgj_preboot_record))Job=(CONST VOID *)Header;
    }
#endif
  }
  BOOLEAN Valid=Finished && Count==1 && Receipt &&
    lgbs_record_valid(Receipt,sizeof(*Receipt),mHandoff->BootId) && Receipt->mode==LANCELOT_GPU_BOOTSTRAP_MODE;
  if(Valid) {
    Log("GPU_PREBOOT_RECEIPT bytes=%u boot=%016lx flags=0x%x gpu_id=0x%x id_valid=%u restore=%016lx scope_return=%016lx writes=%u trace=%u trace_dropped=%u runtime_grant=0\r\n",
      (UINT32)sizeof(*Receipt),Receipt->boot_id,Receipt->flags,Receipt->gpu_id,Receipt->gpu_id_valid,
      Receipt->restore_status,Receipt->release_status,Receipt->write_attempts,Receipt->trace_count,Receipt->trace_dropped);
    for(UINTN I=0;I<LGBS_STAGES;++I) {
      CONST struct lgbs_step *Step=&Receipt->steps[I];
      Log("GPU_PREBOOT_STEP index=%u executed=%u completed=%u status=%016lx start_us=%Lu end_us=%Lu reads=%u writes=%u\r\n",
        (UINT32)I,Step->executed,Step->completed,Step->status,Step->start_us,Step->end_us,Step->reads,Step->writes);
    }
  } else Log("GPU_PREBOOT_RECEIPT valid=0 count=%u inventory_finished=%u status=%r\r\n",Count,Finished,Status);
  BOOLEAN SafeUnload=Valid && (Receipt->flags&LGBS_BOOT_MAY_CONTINUE) && !(Receipt->flags&LGBS_TERMINAL_UNKNOWN);
  BOOLEAN JobPass=TRUE;
#if LANCELOT_GPU_BOOTSTRAP_MODE == 3
  BOOLEAN JobValid=Finished && JobCount==1 && Job && lgj_preboot_valid(Job,sizeof(*Job),mHandoff->BootId);
  if(JobValid) {
    GpuJobRawReceipt(Job);
    Log("GPU_PREBOOT_JOB_RECEIPT bytes=%u used=%u error=%d flags=0x%x GPU_reads=%u GPU_writes=%u fence=%Lu completion_generation=%Lu CPU_value=0x%x pages_returned=%u power_returned=%u retained=%u trace=%u dropped=%u runtime_grant=0\r\n",
      (UINT32)sizeof(*Job),Job->report.used,Job->report.error,Job->report.flags,Job->report.gpu_reads,Job->report.gpu_writes,
      Job->report.fence,Job->report.completion_generation,Job->report.observed_value,Job->report.memory_returned,
      Job->report.power_returned,Job->report.resources_retained,Job->report.trace_count,Job->report.trace_dropped);
    for(UINTN I=0;I<LGJ_STAGES;++I) { CONST struct lgj_step *Step=&Job->report.steps[I];
      Log("GPU_PREBOOT_JOB_STEP index=%u executed=%u completed=%u error=%d status=%016lx reads=%u writes=%u\r\n",
        (UINT32)I,Step->executed,Step->completed,Step->error,Step->adapter_status,Step->reads,Step->writes);
    }
  } else Log("GPU_PREBOOT_JOB_RECEIPT valid=0 count=%u runtime_grant=0\r\n",JobCount);
  SafeUnload=SafeUnload && JobValid && !Job->report.resources_retained && !Job->report.backing_retained;
  JobPass=JobValid && Job->report.used && Job->report.error==0 && Job->report.memory_returned &&
    Job->report.power_returned && (Job->report.flags&LGJ_READBACK_VALID) &&
    (Job->report.flags&LGJ_RESOURCES_RETURNED) && !(Job->report.flags&LGJ_UNKNOWN) &&
    !Job->report.resources_retained && Valid && Job->report.gpu_id==Receipt->gpu_id &&
    Job->source_generation==Receipt->source_generation;
#endif
  /* Unknown DMA keeps the loaded image/backing. An exit/ACPI copy cannot
   * release it; Windows/EBS and guessed recovery stay forbidden. */
  if(Child && SafeUnload)gBS->UnloadImage(Child);
  else Log("GPU_PREBOOT_CHILD_RETAINED image=1 no_force_unload=1 runtime_grant=0\r\n");
  if(ChildStatus!=EFI_SUCCESS)return ChildStatus;
  if(!Valid || !JobPass || !(Receipt->flags&LGBS_BOOT_MAY_CONTINUE) ||
     !(Receipt->flags&LGBS_RETURN_VERIFIED) || !Receipt->gpu_id_valid)return EFI_ABORTED;
  Log("GPU_PREBOOT_END identity_and_return=1 private_job=%u runtime_grant=0\r\n",LANCELOT_GPU_BOOTSTRAP_MODE==3);return EFI_SUCCESS;
#endif
}
