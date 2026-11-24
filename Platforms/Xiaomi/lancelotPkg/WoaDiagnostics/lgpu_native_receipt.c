/* SPDX-License-Identifier: MIT -- pure receipt validation, no MMIO/DMA/grant. */
#include "lgpu_native_job.h"
unsigned lgj_report_valid(const struct lgj_report *r,uint32_t bytes)
{
    if(!r || bytes!=sizeof(*r) || r->magic!=LGJ_MAGIC || r->size!=sizeof(*r) || r->version!=LGJ_VERSION ||
       r->route<1 || r->route>2 || r->command>LGJ_ONCE || r->request_complete>1 || r->flags&~LGJ_FLAGS ||
       r->stop_stage>LGJ_STAGES || r->trace_count>LGJ_TRACE_MAX || r->used>1 ||
       r->memory_returned>1 || r->power_returned>1 || r->current_command>1 ||
       r->request_admitted>1 || r->status_snapshot>1 || r->repeated_request>1 ||
       r->backing_retained>1 || r->resources_retained>1 || r->used>1)return 0;
    for(unsigned i=0;i<LGJ_STAGES;++i){const struct lgj_step *s=&r->steps[i];
        if(s->reserved || s->executed>1 || s->completed>s->executed ||
           (!s->executed && (s->error!=LGJ_NOT_RUN || s->completed || s->reads || s->writes || s->start_us || s->end_us || s->adapter_status)) ||
           (s->completed && s->end_us<s->start_us))return 0;}
    for(unsigned i=0;i<r->trace_count;++i)if(r->traces[i].kind>LGJ_GUARD || r->traces[i].stage>=LGJ_STAGES || r->traces[i].reserved)return 0;
    if((r->flags&LGJ_UNKNOWN) && ((r->flags&LGJ_SAFE_TO_FREE) || r->memory_returned || r->power_returned))return 0;
    if((r->flags&LGJ_ID_VALID) && (r->gpu_id!=0x74021000u || r->mmu_features!=0x2823u ||
       r->va_bits!=35 || r->pa_bits!=40 || !r->steps[LGJ_FEATURES].completed || r->steps[LGJ_FEATURES].error))return 0;
    if((r->flags&LGJ_FENCE_COMPLETE) && (!(r->flags&LGJ_SUBMITTED) || !r->fence ||
       r->completion_generation!=r->generation || !(r->fence_job_irq&2) || (r->fence_js_status&255u)>1u ||
       !r->steps[LGJ_FENCE].completed || r->steps[LGJ_FENCE].error))return 0;
    if((r->flags&LGJ_READBACK_VALID) && (!(r->flags&LGJ_FENCE_COMPLETE) || !r->guards_valid || !r->table_guards_valid ||
       r->observed_value!=r->expected_value || r->expected_value!=LGJ_VALUE || r->descriptor_exception!=1 ||
       !r->steps[LGJ_CPU_READBACK].completed || r->steps[LGJ_CPU_READBACK].error))return 0;
    if((r->flags&LGJ_READY_TO_RETURN) && (!(r->flags&LGJ_SAFE_TO_FREE) || !r->as_detached || !r->core_off || !r->config_restored || (r->flags&LGJ_AS_LIVE)))return 0;
    const unsigned proof[]={LGJ_DETACH,LGJ_CORE_OFF,LGJ_RESTORE};
    if(r->flags&LGJ_READY_TO_RETURN)for(unsigned i=0;i<3;++i)
        if(!r->steps[proof[i]].completed || r->steps[proof[i]].error)return 0;
    if(r->memory_returned && (!r->steps[LGJ_MEMORY_RETURN].completed || r->steps[LGJ_MEMORY_RETURN].error))return 0;
    if(r->power_returned && (!r->steps[LGJ_POWER_RETURN].completed || r->steps[LGJ_POWER_RETURN].error))return 0;
    if(r->used && r->error==0 && (!(r->flags&LGJ_READBACK_VALID) || !(r->flags&LGJ_READY_TO_RETURN)))return 0;
    return 1;
}
void lgj_seal_preboot(struct lgj_preboot_record *r)
{
    r->checksum=0;uint8_t sum=0;const uint8_t *b=(const uint8_t *)r;
    for(unsigned i=0;i<sizeof(*r);++i)sum=(uint8_t)(sum+b[i]);r->checksum=(uint8_t)(0u-sum);
}
unsigned lgj_preboot_valid(const struct lgj_preboot_record *r,uint32_t bytes,uint64_t boot)
{
    if(!r || bytes!=sizeof(*r) || r->signature!=LGJ_ACPI_SIGNATURE || r->length!=sizeof(*r) ||
       r->acpi_revision!=1 || r->version!=LGJ_VERSION || r->board_id!=0x6768 ||
       r->route!=LGJ_ROUTE_PREBOOT || r->reserved || r->padding || !boot || r->boot_id!=boot ||
       r->oem_revision!=1 || r->creator_id!=0x5550474cu || r->creator_revision!=1 ||
       !r->source_generation || r->report.route!=LGJ_ROUTE_PREBOOT)return 0;
    const uint8_t oem[6]={'L','N','C','L','T',' '},table[8]={'G','P','U','J','O','B','0','1'};
    for(unsigned i=0;i<6;++i)if(r->oem_id[i]!=oem[i])return 0;
    for(unsigned i=0;i<8;++i)if(r->oem_table_id[i]!=table[i])return 0;
    unsigned sum=0,source=0,image=0;const uint8_t *b=(const uint8_t *)r;
    for(unsigned i=0;i<bytes;++i)sum+=b[i];if(sum&255)return 0;
    for(unsigned i=0;i<32;++i){source|=r->firmware_source_sha256[i];image|=r->firmware_text_sha256[i];}
    return source && image && lgj_report_valid(&r->report,sizeof(r->report));
}
