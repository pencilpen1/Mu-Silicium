/* SPDX-License-Identifier: MIT
 * Independently implemented MT6768 preboot identity lifecycle. Register facts
 * are reviewed against Android clk-mt6768-pg.c / mtk_gpufreq_core.h. The iOS
 * loader's AP/TPL, checked PWRAP and return path are architectural references.
 * No DVFS, shared PLL/PMIC configuration, global SPM key, DMA, IRQ or GPU write.
 */
#include "lgpu_bootstrap.h"
#define DOMAIN_MASK 0x7800u
#define PROTECT_MASK 0x02600000u
#define PROTECT1 0x02000000u
#define PROTECT2 0x00600000u
#define RST 1u
#define ISO 2u
#define ON 4u
#define ON2 8u
#define CLOCK_DIS 16u
#define SRAM_PDN 0x100u
#define SRAM_ACK 0x1000u
static const uint32_t bases[LGBS_BANKS]={0x10006000u,0x10001000u,0x13000000u,0x13040000u,0x10000000u,0x1000c000u};
/* Parent before children; ASYNC has no SRAM bit. */
static const uint32_t ctrl[4]={0x334u,0x338u,0x34cu,0x310u};
static const uint32_t domain[4]={0x4000u,0x0800u,0x1000u,0x2000u};
uint16_t lgbs_pmic_address(unsigned i)
{
    const uint16_t a[LGBS_PMIC_FIELDS]={0x000a,0x0008,0x1508,0x1520,0x1526,0x151e,0x1828,
        0x1bc8,0x1be4,0x1bf4,0x1be2,0x1bdc};
    return i<LGBS_PMIC_FIELDS ? a[i] : 0;
}
uint32_t lgbs_clock_offset(unsigned i)
{
    const uint32_t a[LGBS_CLOCK_FIELDS]={0x50,0x60,0x248,0x24c,0x254,0x258,0x25c,0x264,0x238,0x23c,0x244};
    return i<LGBS_CLOCK_FIELDS ? a[i] : 0;
}
uint32_t lgbs_aon_offset(unsigned i)
{
    const uint32_t a[LGBS_AON_FIELDS]={0x180,0x184,0x334,0x338,0x34c,0x310,0x220,0x228};
    return i<LGBS_AON_FIELDS ? a[i] : 0;
}
const char *lgbs_stage_name(unsigned i)
{
    const char *const n[LGBS_STAGES]={"existing_uefi_protocols","preboot_ap_tpl_scope","pmic_readback",
        "selected_clock_readback","spm_infra_preflight","cold_mfg_power_on","gpu_id_read_once",
        "cold_mfg_power_return","platform_return_readback","restore_original_tpl"};
    return i<LGBS_STAGES ? n[i] : "not_run";
}
static uint64_t tick(struct lgbs_context *c)
{
    uint64_t t=c->io.now_us(c->io.context);
    if(!t || t<c->last_us)return 0;
    c->last_us=t;return t;
}
static uint64_t guard(struct lgbs_context *c)
{
    uint64_t t=tick(c),deadline=c->record.deadline_us;
    if(!c->restoring)deadline-=LGBS_RESTORE_RESERVE_US;
    if(!t || t>=deadline || t<c->step_started_us || t-c->step_started_us>=LGBS_STEP_US)return LGBS_TIMEOUT;
    if(c->io_failed || c->scope_lost)return LGBS_ABORTED;
    return LGBS_SUCCESS;
}
static void trace(struct lgbs_context *c,unsigned kind,uint32_t address,uint32_t value,uint64_t status,unsigned valid)
{
    struct lgbs_record *r=&c->record;
    if((kind!=LGBS_AFFINITY && status) || (kind==LGBS_AFFINITY && !valid))r->failed_address=address;
    if(r->trace_count<LGBS_TRACES)r->traces[r->trace_count++]=(struct lgbs_trace){kind,address,value,valid,status,c->last_us};
    else ++r->trace_dropped;
}
static uint64_t begin(struct lgbs_context *c,unsigned stage)
{
    struct lgbs_step *s=&c->record.steps[stage];
    if(s->executed)return LGBS_INVALID;
    c->current_stage=stage;c->step_started_us=tick(c);
    s->executed=1;s->start_us=c->step_started_us;s->reads=c->record.read_attempts+c->record.pmic_read_attempts;
    s->writes=c->record.write_attempts;
    if(c->io.stage_event)c->io.stage_event(c->io.context,stage,0,LGBS_NOT_RUN);
    return guard(c);
}
static uint64_t finish(struct lgbs_context *c,uint64_t status)
{
    struct lgbs_step *s=&c->record.steps[c->current_stage];
    s->end_us=tick(c);s->completed=1;s->status=status;
    s->reads=c->record.read_attempts+c->record.pmic_read_attempts-s->reads;
    s->writes=c->record.write_attempts-s->writes;
    if(status==LGBS_SUCCESS && guard(c)!=LGBS_SUCCESS)s->status=status=LGBS_TIMEOUT;
    if(status && c->record.failed_stage==LGBS_STAGES)c->record.failed_stage=c->current_stage;
    if(c->io.stage_event)c->io.stage_event(c->io.context,c->current_stage,1,status);
    return status;
}
static uint64_t rd(struct lgbs_context *c,enum lgbs_bank bank,uint32_t off,uint32_t *v)
{
    uint64_t s=guard(c);if(s)return s;
    if(bank>=LGBS_BANKS || off&3u || off>=(bank==LGBS_GPU?0x4000u:0x1000u))return LGBS_INVALID;
    uint32_t value=0;++c->record.read_attempts;
    if(bank==LGBS_GPU && off==0)c->record.flags|=LGBS_ID_READ;
    s=c->io.read32(c->io.context,bank,off,&value);
    if(bank==LGBS_GPU && off==0 && s==LGBS_SUCCESS)c->record.gpu_id=value;
    if(s==LGBS_IO || s==LGBS_TIMEOUT)c->io_failed=1;
    uint64_t time_status=guard(c);trace(c,LGBS_MMIO_READ,bases[bank]+off,value,s,s==LGBS_SUCCESS);
    if(s)return s;if(time_status)return time_status;*v=value;return LGBS_SUCCESS;
}
static uint64_t wr(struct lgbs_context *c,enum lgbs_bank bank,uint32_t off,uint32_t value)
{
    uint64_t s=guard(c);if(s)return s;
    /* Structural write whitelist. Never use a global SPM unlock or PLL/GPU/PMIC write. */
    unsigned permitted=bank==LGBS_MFG && (off==4 || off==8) && value==1;
    if(bank==LGBS_INFRA)permitted=(off==0x2a0 || off==0x2a4) && (value==PROTECT1 || value==PROTECT2);
    if(bank==LGBS_SPM)for(unsigned i=0;i<4;++i)if(off==ctrl[i])permitted=1;
    if(!permitted)return LGBS_INVALID;
    ++c->record.write_attempts;c->record.flags|=LGBS_MUTATION_ISSUED;
    s=c->io.write32(c->io.context,bank,off,value);if(s)c->io_failed=1;
    uint64_t time_status=guard(c);trace(c,LGBS_MMIO_WRITE,bases[bank]+off,value,s,s==LGBS_SUCCESS);
    return s ? s : time_status;
}
static uint64_t scope(struct lgbs_context *c,unsigned final)
{
    struct lgbs_scope_observation o={0};++c->record.scope_checks;
    uint64_t s=guard(c);if(s)return s;
    s=c->io.scope(c->io.context,&o);
    uint64_t time_status=guard(c);
    for(unsigned i=0;i<8;++i)trace(c,LGBS_AFFINITY,i<<8,(uint32_t)o.affinity[i],(uint64_t)o.affinity[i],
                                  o.affinity[i]>=0 && o.affinity[i]<=2);
    struct lgbs_record *r=&c->record;
    if(final){r->ap_off_after=o.ap_off_mask;r->mpidr_after=o.mpidr;}
    else {r->ap_off_before=o.ap_off_mask;r->mpidr_before=o.mpidr;}
    r->ap_on_mask|=o.ap_on_mask;r->ap_unknown_mask|=o.ap_unknown_mask;
    if(s)return s;if(time_status)return time_status;
    if(o.mpidr>0x700 || (o.mpidr&255u) || !o.tpl_held || o.ap_on_mask || o.ap_unknown_mask ||
       o.ap_off_mask!=(255u^(1u<<(o.mpidr>>8))) || (final && r->mpidr_before!=o.mpidr)) {
        c->scope_lost=1;return LGBS_NOT_READY;
    }
    r->flags|=LGBS_AP_OFF_VERIFIED|LGBS_TPL_HELD;return LGBS_SUCCESS;
}
#define DO(x) do {uint64_t e=(x);if(e)return e;}while(0)
static uint64_t pmic_snapshot(struct lgbs_context *c,uint16_t out[LGBS_PMIC_FIELDS])
{
    for(unsigned i=0;i<LGBS_PMIC_FIELDS;++i) {
        DO(guard(c));uint16_t v=0;++c->record.pmic_read_attempts;
        uint64_t s=c->io.pmic_read16(c->io.context,lgbs_pmic_address(i),&v);
        if(s==LGBS_IO || s==LGBS_TIMEOUT)c->io_failed=1;
        uint64_t t=guard(c);trace(c,LGBS_PMIC_READ,lgbs_pmic_address(i),v,s,s==LGBS_SUCCESS);
        if(s)return s;if(t)return t;out[i]=v;
        if(i==0 && v!=0x5810 && v!=0x5820 && v!=0x5830)return LGBS_UNSUPPORTED;
    }
    return LGBS_SUCCESS;
}
static uint64_t clock_snapshot(struct lgbs_context *c,uint32_t out[LGBS_CLOCK_FIELDS])
{
    for(unsigned i=0;i<LGBS_CLOCK_FIELDS;++i)DO(rd(c,i<2?LGBS_TOP:LGBS_APMIXED,lgbs_clock_offset(i),&out[i]));
    return LGBS_SUCCESS;
}
static uint64_t aon_snapshot(struct lgbs_context *c,uint32_t out[LGBS_AON_FIELDS])
{
    for(unsigned i=0;i<LGBS_AON_FIELDS;++i)DO(rd(c,i<6?LGBS_SPM:LGBS_INFRA,lgbs_aon_offset(i),&out[i]));
    return LGBS_SUCCESS;
}
static uint32_t rail_floor(uint32_t hz)
{
    /* Stock configuration/OPP units are kHz and uV; never called a measurement. */
    const uint32_t khz[30]={950000,925000,900000,875000,850000,823000,796000,769000,743000,716000,
        690000,663000,637000,611000,586000,560000,535000,509000,484000,467000,450000,434000,
        417000,400000,383000,366000,349000,332000,315000,299000};
    const uint32_t uv[30]={900000,875000,850000,825000,800000,793750,781250,768750,756250,750000,
        737500,725000,712500,706250,700000,693750,687500,681250,668750,668750,662500,656250,
        650000,643750,643750,637500,631250,625000,618750,612500};
    for(unsigned i=30;i-- >0;)if(hz<=khz[i]*1000u)return uv[i];
    return UINT32_MAX;
}
static uint64_t decode(struct lgbs_context *c)
{
    struct lgbs_record *r=&c->record;const uint16_t *p=r->pmic_before;const uint32_t *k=r->clock_before;
    if(!(p[2]&1u) || !(p[3]&1u) || !(p[7]&1u) || !(p[8]&1u) ||
       (p[4]&127u)!=(p[5]&127u) || (p[9]&127u)!=((p[10]>>8)&127u))return LGBS_NOT_READY;
    r->vgpu_uv=500000u+(p[5]&127u)*6250u;r->vsram_uv=500000u+((p[10]>>8)&127u)*6250u;
    unsigned parent=k[0]&3u;if(k[0]&0x80u)return LGBS_NOT_READY;
    if(parent==0)r->configured_hz=26000000u;
    else {
        unsigned i=2+3*(parent-1);if(!(k[i]&1u) || (k[i+2]&3u)!=1u)return LGBS_NOT_READY;
        uint32_t d=1u<<((k[i+1]>>24)&7u),factor=parent==1?1u:parent==2?3u:6u;
        uint64_t den=UINT64_C(16384)*d*factor;
        uint64_t hz=(UINT64_C(26000000)*(k[i+1]&0x3fffffu)+den-1)/den;
        if(!hz || hz>950000000u)return LGBS_UNSUPPORTED;r->configured_hz=(uint32_t)hz;
    }
    if(r->vgpu_uv<rail_floor(r->configured_hz) || r->vgpu_uv>950000u ||
       r->vsram_uv<850000u || r->vsram_uv>1050000u || r->vsram_uv<r->vgpu_uv ||
       r->vsram_uv-r->vgpu_uv<100000u || r->vsram_uv-r->vgpu_uv>250000u)return LGBS_UNSUPPORTED;
    r->flags|=LGBS_RAILS_VALID|LGBS_CLOCK_VALID;return LGBS_SUCCESS;
}
static uint64_t stable_resources(struct lgbs_context *c)
{
    uint16_t pmic[LGBS_PMIC_FIELDS];uint32_t clock[LGBS_CLOCK_FIELDS];
    DO(pmic_snapshot(c,pmic));DO(clock_snapshot(c,clock));
    const uint16_t masks[LGBS_PMIC_FIELDS]={0xffff,0xffff,1,1,127,127,4,1,1,127,0x7f00,0x100};
    for(unsigned i=0;i<LGBS_PMIC_FIELDS;++i)if((pmic[i]^c->record.pmic_before[i])&masks[i])return LGBS_NOT_READY;
    unsigned parent=c->record.clock_before[0]&3u;
    if((clock[0]^c->record.clock_before[0])&0x83u)return LGBS_NOT_READY;
    if(parent)for(unsigned j=0;j<3;++j) {
        unsigned i=2+3*(parent-1)+j;uint32_t mask=j==0?1u:j==1?0x073fffffu:3u;
        if((clock[i]^c->record.clock_before[i])&mask)return LGBS_NOT_READY;
    }
    return LGBS_SUCCESS;
}
static uint64_t preflight(struct lgbs_context *c)
{
    struct lgbs_record *r=&c->record;DO(aon_snapshot(c,r->aon_before));
    uint32_t key;DO(rd(c,LGBS_SPM,0,&key));
    uint32_t wanted=r->mode>=LGBS_COLD_IDENTITY_ONCE ? 0 : DOMAIN_MASK;
    if((r->aon_before[0]&DOMAIN_MASK)!=wanted || (r->aon_before[1]&DOMAIN_MASK)!=wanted)return LGBS_NOT_READY;
    for(unsigned i=0;i<4;++i) {
        uint32_t mask=0x1fu | (i?SRAM_PDN|SRAM_ACK:0);
        uint32_t expect=wanted ? RST|ON|ON2 : ISO|CLOCK_DIS|(i?SRAM_PDN|SRAM_ACK:0);
        if((r->aon_before[2+i]&mask)!=expect)return LGBS_NOT_READY;
    }
    uint32_t protect=r->aon_before[6]&PROTECT_MASK;
    if((r->aon_before[7]&PROTECT_MASK)!=protect ||
       (wanted && protect) || (!wanted && protect && protect!=PROTECT_MASK))return LGBS_NOT_READY;
    if(r->mode>=LGBS_COLD_IDENTITY_ONCE && !(key&1u))return LGBS_UNSUPPORTED;
    if(wanted)r->flags|=LGBS_ISLAND_LIVE;
    return LGBS_SUCCESS;
}
static uint64_t modify(struct lgbs_context *c,unsigned i,uint32_t clear,uint32_t set)
{
    uint32_t v;DO(rd(c,LGBS_SPM,ctrl[i],&v));
    if((clear|set)&~(0x1fu|(i?SRAM_PDN:0)) || clear&set)return LGBS_INVALID;
    return wr(c,LGBS_SPM,ctrl[i],(v&~clear)|set);
}
static uint64_t poll(struct lgbs_context *c,enum lgbs_bank bank,uint32_t off,uint32_t mask,uint32_t want,uint32_t second)
{
    uint64_t start=tick(c);if(!start)return LGBS_TIMEOUT;
    for(unsigned i=0;i<LGBS_MAX_POLLS;++i) {
        uint32_t a,b;DO(rd(c,bank,off,&a));b=a;if(second)DO(rd(c,bank,second,&b));
        uint64_t t=tick(c);if(!t || t-start>=LGBS_ACK_US)return LGBS_TIMEOUT;
        if((a&mask)==want && (b&mask)==want)return LGBS_SUCCESS;
        if(i+1<LGBS_MAX_POLLS){DO(guard(c));c->io.delay_us(c->io.context,10);DO(guard(c));}
    }
    return LGBS_TIMEOUT;
}
static uint64_t domain_on(struct lgbs_context *c,unsigned i)
{
    DO(modify(c,i,0,ON));DO(modify(c,i,0,ON2));
    DO(poll(c,LGBS_SPM,0x180,domain[i],domain[i],0x184));
    DO(modify(c,i,CLOCK_DIS,0));DO(modify(c,i,ISO,0));DO(modify(c,i,0,RST));
    if(i){DO(modify(c,i,SRAM_PDN,0));DO(poll(c,LGBS_SPM,ctrl[i],SRAM_ACK,0,0));}
    return LGBS_SUCCESS;
}
static uint64_t domain_off(struct lgbs_context *c,unsigned i)
{
    if(i){DO(modify(c,i,0,SRAM_PDN));DO(poll(c,LGBS_SPM,ctrl[i],SRAM_ACK,SRAM_ACK,0));}
    DO(modify(c,i,0,ISO));DO(modify(c,i,0,CLOCK_DIS));DO(modify(c,i,RST,0));
    DO(modify(c,i,ON,0));DO(modify(c,i,ON2,0));
    return poll(c,LGBS_SPM,0x180,domain[i],0,0x184);
}
static uint64_t protect(struct lgbs_context *c,uint32_t mask,unsigned on)
{
    DO(wr(c,LGBS_INFRA,on?0x2a0:0x2a4,mask));
    return poll(c,LGBS_INFRA,0x220,mask,on?mask:0,0x228);
}
static uint64_t power_on(struct lgbs_context *c)
{
    DO(scope(c,1));DO(stable_resources(c));
    /* The iOS caller permits cold/off with protection either all0 or all1,
     * provided enable and ACK agree. Normalize only our exact GPU bits and
     * remember the original0 state for a verified return after domains OFF. */
    if(!(c->record.aon_before[6]&PROTECT_MASK)){DO(protect(c,PROTECT1,1));DO(protect(c,PROTECT2,1));}
    DO(domain_on(c,0));DO(domain_on(c,1));
    DO(protect(c,PROTECT2,0));DO(protect(c,PROTECT1,0));
    DO(domain_on(c,2));DO(domain_on(c,3));
    DO(rd(c,LGBS_MFG,0,&c->record.mfg_gate_before));c->record.mfg_gate_before_valid=1;
    DO(wr(c,LGBS_MFG,8,1));DO(poll(c,LGBS_MFG,0,1,0,0));
    c->record.flags|=LGBS_ISLAND_LIVE;return LGBS_SUCCESS;
}
static uint64_t identity(struct lgbs_context *c)
{
    DO(scope(c,1));DO(stable_resources(c));uint32_t s1,s2,v;DO(rd(c,LGBS_SPM,0x180,&s1));DO(rd(c,LGBS_SPM,0x184,&s2));
    if((s1&DOMAIN_MASK)!=DOMAIN_MASK || (s2&DOMAIN_MASK)!=DOMAIN_MASK)return LGBS_NOT_READY;
    for(unsigned i=0;i<4;++i){DO(rd(c,LGBS_SPM,ctrl[i],&v));if((v&(0x1fu|(i?SRAM_PDN|SRAM_ACK:0)))!=(RST|ON|ON2))return LGBS_NOT_READY;}
    DO(rd(c,LGBS_INFRA,0x220,&v));if(v&PROTECT_MASK)return LGBS_NOT_READY;
    DO(rd(c,LGBS_INFRA,0x228,&v));if(v&PROTECT_MASK)return LGBS_NOT_READY;
    DO(rd(c,LGBS_MFG,0,&v));if(v&1u)return LGBS_NOT_READY;
    /* Immutable ID only; GPU commands/status/IRQ/address spaces are untouched. */
    uint64_t s=rd(c,LGBS_GPU,0,&v);
    if(!s){c->record.gpu_id=v;c->record.flags|=LGBS_ID_READ;c->record.gpu_id_valid=v && v!=UINT32_MAX;
        if(c->record.gpu_id_valid)c->record.flags|=LGBS_ID_VALID;}
    return s ? s : c->record.gpu_id_valid && (v&0xffff0000u)==0x74020000u ? LGBS_SUCCESS : LGBS_UNSUPPORTED;
}
static uint64_t power_off(struct lgbs_context *c)
{
    DO(scope(c,1));DO(stable_resources(c));DO(wr(c,LGBS_MFG,4,1));DO(poll(c,LGBS_MFG,0,1,1,0));
    DO(domain_off(c,3));DO(domain_off(c,2));DO(protect(c,PROTECT1,1));DO(protect(c,PROTECT2,1));
    DO(domain_off(c,1));DO(domain_off(c,0));c->record.flags&=~LGBS_ISLAND_LIVE;
    if(!(c->record.aon_before[6]&PROTECT_MASK)){DO(protect(c,PROTECT2,0));DO(protect(c,PROTECT1,0));}
    return LGBS_SUCCESS;
}
static uint64_t readback(struct lgbs_context *c)
{
    struct lgbs_record *r=&c->record;DO(scope(c,1));DO(aon_snapshot(c,r->aon_after));
    DO(clock_snapshot(c,r->clock_after));DO(pmic_snapshot(c,r->pmic_after));
    for(unsigned i=0;i<LGBS_AON_FIELDS;++i) {
        uint32_t mask=i<2?DOMAIN_MASK:i<6?(0x1fu|(i>2?SRAM_PDN|SRAM_ACK:0)):PROTECT_MASK;
        if((r->aon_before[i]^r->aon_after[i])&mask)return LGBS_IO;
    }
    unsigned parent=r->clock_before[0]&3u;
    if((r->clock_before[0]^r->clock_after[0])&0x83u)return LGBS_IO;
    if(parent)for(unsigned j=0;j<3;++j){unsigned i=2+3*(parent-1)+j;
        uint32_t mask=j==0?1u:j==1?0x073fffffu:3u;if((r->clock_before[i]^r->clock_after[i])&mask)return LGBS_IO;}
    const uint16_t masks[LGBS_PMIC_FIELDS]={0xffff,0xffff,1,1,127,127,4,1,1,127,0x7f00,0x100};
    for(unsigned i=0;i<LGBS_PMIC_FIELDS;++i)if((r->pmic_before[i]^r->pmic_after[i])&masks[i])return LGBS_IO;
    r->flags|=LGBS_RETURN_VERIFIED;return LGBS_SUCCESS;
}
static uint64_t step(struct lgbs_context *c,unsigned stage,uint64_t (*work)(struct lgbs_context *))
{
    uint64_t s=begin(c,stage);return finish(c,s ? s : work(c));
}
static uint64_t initial_scope(struct lgbs_context *c){return scope(c,0);}
static uint64_t initial_pmic(struct lgbs_context *c){return pmic_snapshot(c,c->record.pmic_before);}
static uint64_t initial_clock(struct lgbs_context *c){DO(clock_snapshot(c,c->record.clock_before));return decode(c);}
uint64_t lgbs_run_work(struct lgbs_context *c,const struct lgbs_ops *io,uint32_t mode,uint64_t boot,uint64_t generation,uint64_t frequency,const struct lgbs_private_work *work)
{
    if(!c || !io || c->used)return LGBS_INVALID;
    /* Preserve genuine provenance supplied by the UEFI caller; no identity is minted. */
    uint8_t source[32],image[32];for(unsigned i=0;i<32;++i){source[i]=c->record.firmware_source_sha256[i];image[i]=c->record.firmware_text_sha256[i];}
    c->record=(struct lgbs_record){.signature=LGBS_SIGNATURE,.length=sizeof(c->record),.acpi_revision=1,
        .oem_id={'L','N','C','L','T',' '},.oem_table_id={'G','P','U','B','S','0','0','1'},
        .oem_revision=1,.creator_id=0x5550474cu,.creator_revision=1,.version=LGBS_VERSION,.board_id=LGBS_BOARD,
        .mode=mode,.phase=LGBS_PHASE_RUNNING,.stage_count=LGBS_STAGES,.boot_id=boot,.source_generation=generation,
        .counter_frequency=frequency,.status=LGBS_NOT_RUN,.restore_status=LGBS_NOT_RUN,.release_status=LGBS_NOT_RUN,
        .failed_stage=LGBS_STAGES};
    for(unsigned i=0;i<32;++i){c->record.firmware_source_sha256[i]=source[i];c->record.firmware_text_sha256[i]=image[i];}
    for(unsigned i=0;i<LGBS_STAGES;++i)c->record.steps[i].status=LGBS_NOT_RUN;
    c->used=1;c->io=*io;
    uint64_t status=LGBS_INVALID;
    if(!io->now_us || !io->delay_us || !io->scope || !io->scope_return || !io->pmic_read16 ||
       !io->read32 || !io->write32 || !boot || !generation || !frequency || frequency>1000000000u ||
       mode>LGBS_COLD_PRIVATE_JOB_ONCE ||
       ((mode==LGBS_COLD_PRIVATE_JOB_ONCE)!=!!work) ||
       (work && (!work->context || !work->run)))goto done;
    unsigned source_present=0,image_present=0;
    for(unsigned i=0;i<32;++i){source_present|=source[i];image_present|=image[i];}
    if(!source_present || !image_present)goto done;
    c->record.start_us=tick(c);
    if(!c->record.start_us || c->record.start_us>UINT64_MAX-LGBS_TOTAL_US){status=LGBS_TIMEOUT;goto done;}
    c->record.deadline_us=c->record.start_us+LGBS_TOTAL_US;
    c->record.steps[LGBS_PROTOCOL]=(struct lgbs_step){.executed=1,.completed=1,
        .status=LGBS_SUCCESS,.start_us=c->record.start_us,.end_us=c->record.start_us};
    if(mode==LGBS_DISABLED){status=LGBS_SUCCESS;c->record.flags|=LGBS_BOOT_MAY_CONTINUE;goto done;}
    c->record.flags|=LGBS_REQUESTED;
    status=step(c,LGBS_SCOPE,initial_scope);if(status)goto leave;
    status=step(c,LGBS_PMIC,initial_pmic);if(status)goto leave;
    status=step(c,LGBS_CLOCK,initial_clock);if(status)goto leave;
    status=step(c,LGBS_PREFLIGHT,preflight);if(status)goto leave;
    if(mode>=LGBS_COLD_IDENTITY_ONCE){status=step(c,LGBS_POWER_ON,power_on);if(status)goto leave;}
    status=step(c,LGBS_IDENTITY,identity);
    /* A completed immutable read with a zero/unknown ID may still be returned.
     * Transport errors, lost scope or unfinished power transitions never get
     * guessed recovery writes or a fresh timeout budget. */
    if(c->io_failed || c->scope_lost)goto leave;
    if(mode==LGBS_COLD_PRIVATE_JOB_ONCE && status==LGBS_SUCCESS) {
        unsigned safe=0;
        status=work->run(work->context,c->record.gpu_id,c->record.deadline_us,&safe);
        if(safe!=1u){if(status==LGBS_SUCCESS)status=LGBS_INVALID;
            c->record.flags|=LGBS_TERMINAL_UNKNOWN;goto leave;}
    }
    c->restoring=1;
    if(mode>=LGBS_COLD_IDENTITY_ONCE){c->record.restore_status=step(c,LGBS_POWER_OFF,power_off);if(c->record.restore_status)goto leave;}
    c->record.restore_status=step(c,LGBS_READBACK,readback);
    if(c->record.restore_status!=LGBS_SUCCESS)c->record.flags&=~LGBS_RETURN_VERIFIED;
leave:
    c->restoring=1;
    if(c->io_failed || ((c->record.flags&LGBS_MUTATION_ISSUED) && !(c->record.flags&LGBS_RETURN_VERIFIED)))
        c->record.flags|=LGBS_TERMINAL_UNKNOWN;
    /* Restore the exact caller TPL, even for a clean refusal. Returning the
     * scheduling scope does NOT release unknown hardware or authorize boot. */
    {
        struct lgbs_step *s=&c->record.steps[LGBS_SCOPE_RETURN];
        s->executed=1;s->start_us=tick(c);
        if(c->io.stage_event)c->io.stage_event(c->io.context,LGBS_SCOPE_RETURN,0,LGBS_NOT_RUN);
        uint64_t release=io->scope_return(io->context);
        c->record.release_status=release;
        s->completed=1;s->end_us=tick(c);s->status=release;
        if(c->io.stage_event)c->io.stage_event(c->io.context,LGBS_SCOPE_RETURN,1,release);
        if(!release){c->record.flags|=LGBS_SCOPE_RETURNED;c->record.flags&=~LGBS_TPL_HELD;}
    }
    if(!(c->record.flags&LGBS_TERMINAL_UNKNOWN) && c->record.release_status==LGBS_SUCCESS)
        c->record.flags|=LGBS_BOOT_MAY_CONTINUE;
    if(status==LGBS_SUCCESS && c->record.restore_status!=LGBS_SUCCESS)status=c->record.restore_status;
    if(status==LGBS_SUCCESS && c->record.release_status!=LGBS_SUCCESS)status=c->record.release_status;
done:
    c->record.status=status;c->record.end_us=io->now_us ? tick(c) : 0;
    c->record.phase=(c->record.flags&LGBS_TERMINAL_UNKNOWN)?LGBS_PHASE_TERMINAL:LGBS_PHASE_RETURNED;
    lgbs_seal(&c->record);return status;
}
uint64_t lgbs_run(struct lgbs_context *c,const struct lgbs_ops *io,uint32_t mode,uint64_t boot,uint64_t generation,uint64_t frequency)
{return lgbs_run_work(c,io,mode,boot,generation,frequency,NULL);}
void lgbs_seal(struct lgbs_record *r)
{
    r->checksum=0;uint8_t sum=0;const uint8_t *p=(const uint8_t *)r;
    for(unsigned i=0;i<sizeof(*r);++i)sum=(uint8_t)(sum+p[i]);r->checksum=(uint8_t)(0u-sum);
}
unsigned lgbs_record_valid(const struct lgbs_record *r,uint32_t bytes,uint64_t boot)
{
    if(!r || bytes!=sizeof(*r) || r->signature!=LGBS_SIGNATURE || r->length!=sizeof(*r) ||
       r->acpi_revision!=1 || r->version!=LGBS_VERSION || r->board_id!=LGBS_BOARD ||
       r->oem_revision!=1 || r->creator_id!=0x5550474cu || r->creator_revision!=1 ||
       r->mode>LGBS_COLD_PRIVATE_JOB_ONCE || r->phase<LGBS_PHASE_RETURNED || r->phase>LGBS_PHASE_TERMINAL ||
       !boot || r->boot_id!=boot || !r->source_generation || !r->counter_frequency || r->counter_frequency>1000000000u ||
       r->stage_count!=LGBS_STAGES || r->trace_count>LGBS_TRACES || r->flags&~LGBS_FLAGS)return 0;
    const uint8_t *p=(const uint8_t *)r;unsigned sum=0;
    for(unsigned i=0;i<sizeof(*r);++i)sum+=p[i];if(sum&255u)return 0;
    const uint8_t oem[6]={'L','N','C','L','T',' '},table[8]={'G','P','U','B','S','0','0','1'};
    for(unsigned i=0;i<6;++i)if(r->oem_id[i]!=oem[i])return 0;
    for(unsigned i=0;i<8;++i)if(r->oem_table_id[i]!=table[i])return 0;
    unsigned source=0,image=0;
    for(unsigned i=0;i<32;++i){source|=r->firmware_source_sha256[i];image|=r->firmware_text_sha256[i];}
    if(!source || !image)return 0;
    for(unsigned i=0;i<LGBS_STAGES;++i) {
        const struct lgbs_step *s=&r->steps[i];
        if(s->executed>1 || s->completed>1 || s->completed>s->executed)return 0;
        if(!s->executed && (s->completed || s->status!=LGBS_NOT_RUN || s->start_us || s->end_us || s->reads || s->writes))return 0;
        if(s->completed && s->end_us<s->start_us)return 0;
    }
    if(r->gpu_id_valid>1 || r->gpu_id_valid!=!!(r->flags&LGBS_ID_VALID) ||
       ((r->flags&LGBS_ID_VALID) && (!(r->flags&LGBS_ID_READ) || !r->gpu_id || r->gpu_id==UINT32_MAX)))return 0;
    if((r->flags&LGBS_MUTATION_ISSUED)!= (r->write_attempts ? LGBS_MUTATION_ISSUED : 0))return 0;
    for(unsigned i=0;i<r->trace_count;++i)if(r->traces[i].kind>LGBS_AFFINITY || r->traces[i].valid>1)return 0;
    if((r->flags&LGBS_BOOT_MAY_CONTINUE) && (r->flags&LGBS_TERMINAL_UNKNOWN))return 0;
    if((r->flags&LGBS_ID_READ) && (!r->steps[LGBS_IDENTITY].executed || !r->steps[LGBS_IDENTITY].completed))return 0;
    if((r->flags&LGBS_RETURN_VERIFIED) && (r->restore_status!=LGBS_SUCCESS ||
       r->steps[LGBS_READBACK].status!=LGBS_SUCCESS || !r->steps[LGBS_READBACK].completed))return 0;
    if(r->status==LGBS_SUCCESS && r->mode!=LGBS_DISABLED &&
       (!(r->flags&LGBS_ID_VALID) || (r->gpu_id&0xffff0000u)!=0x74020000u ||
        !(r->flags&LGBS_RETURN_VERIFIED) || !(r->flags&LGBS_SCOPE_RETURNED)))return 0;
    return 1;
}
