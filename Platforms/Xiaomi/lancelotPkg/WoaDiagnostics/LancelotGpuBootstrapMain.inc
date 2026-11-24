/* SPDX-License-Identifier: MIT -- main-owned UEFI integration proposal only.
 * Include after the existing Log() definition in LancelotPreflight.c.
 * Default0 keeps ordinary startup unchanged. Main explicitly builds1 or2.
 */
#include <Protocol/AcpiSystemDescriptionTable.h>
#include "lgpu_bootstrap.h"
#ifndef LANCELOT_GPU_BOOTSTRAP_MODE
#define LANCELOT_GPU_BOOTSTRAP_MODE 2
#endif
#if LANCELOT_GPU_BOOTSTRAP_MODE < 0 || LANCELOT_GPU_BOOTSTRAP_MODE > 2
#error LANCELOT_GPU_BOOTSTRAP_MODE must be0,1,or2
#endif
STATIC EFI_STATUS RunGpuBootstrapBeforeWindows(EFI_HANDLE Parent,EFI_HANDLE Device)
{
#if LANCELOT_GPU_BOOTSTRAP_MODE == 0
  (VOID)Parent;(VOID)Device;return EFI_SUCCESS;
#else
  STATIC EFI_GUID SdtGuid={0xeb97088e,0xcfdf,0x49c6,{0xbe,0x4b,0xd9,0x06,0xa5,0xb2,0x0e,0x86}};
#if LANCELOT_GPU_BOOTSTRAP_MODE == 1
  STATIC CHAR16 Options[]=L"--identity-once";
#else
  STATIC CHAR16 Options[]=L"--power-id-once";
#endif
  EFI_DEVICE_PATH_PROTOCOL *Path;
  EFI_HANDLE Child=NULL;EFI_LOADED_IMAGE_PROTOCOL *Loaded=NULL;
  EFI_ACPI_SDT_PROTOCOL *Sdt=NULL;CONST struct lgbs_record *Receipt=NULL;
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
  }
  BOOLEAN Valid=Finished && Count==1 && Receipt &&
    lgbs_record_valid(Receipt,sizeof(*Receipt),mHandoff->BootId);
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
  /* The application has no persistent callbacks/DMA. Its installed ACPI table
   * is an independent copy. Unload never clears original PWRAP poison or an
   * unknown island state, and does not authorize Windows after a failed test. */
  if(Child)gBS->UnloadImage(Child);
  if(ChildStatus!=EFI_SUCCESS)return ChildStatus;
  if(!Valid || !(Receipt->flags&LGBS_BOOT_MAY_CONTINUE) ||
     !(Receipt->flags&LGBS_RETURN_VERIFIED) || !Receipt->gpu_id_valid)return EFI_ABORTED;
  Log("GPU_PREBOOT_END identity_and_return=1 runtime_grant=0\r\n");return EFI_SUCCESS;
#endif
}
