#include "bt_kernal_target.h"
#include "../public/bt_static_core_internal.h"
#include "../media/bt_sha256.h"
#include "../../Generated/NaturalRepair/Kernal/bt_kernal_aot_generated.h"
#include "../../Generated/NaturalRepair/Fastloader/bt_fastloader_records.h"
#include "../../Generated/NaturalRepair/DriveUpload/bt_drive_upload_generated.h"
#include <string.h>

#define BT_KERNAL_FILE_BUFFER 65536u

static void digest_hex(const uint8_t d[32],char out[65]){
    static const char h[]="0123456789abcdef"; unsigned i;
    for(i=0;i<32u;i++){out[i*2u]=h[d[i]>>4];out[i*2u+1u]=h[d[i]&15u];} out[64]=0;
}
static void set_carry(cpu6510 *c,int yes){if(yes)c->p|=CPU6510_P_C;else c->p&=(uint8_t)~CPU6510_P_C;}

void bt_kernal_target_state_init(bt_kernal_target_state *k){if(k){memset(k,0,sizeof(*k));k->current_input=0xffu;k->current_output=0xffu;}}

/* Apply the target-relevant state established by the pinned 901227-03 KERNAL
   IOINIT routine at $FDA3-$FDDD.  This is not ROM execution: the exact writes
   below are a bounded semantic projection of immutable KERNAL bytes.  In
   particular $DD00=$07/$DD02=$3F makes CIA2 PA3..5 real IEC outputs; without
   this reset state the later statically translated serial routines can change
   PRA but can never drive ATN/CLOCK/DATA. */
static bool apply_kernal_ioinit(bt_static_core *c){
    bt_c64_machine *m;
    if(!c)return false;
    m=&c->machine;
    cia6526_write(&m->cia1,0x0du,0x7fu);
    cia6526_write(&m->cia2,0x0du,0x7fu);
    cia6526_write(&m->cia1,0x00u,0x7fu);
    cia6526_write(&m->cia1,0x0eu,0x08u);
    cia6526_write(&m->cia2,0x0eu,0x08u);
    cia6526_write(&m->cia1,0x0fu,0x08u);
    cia6526_write(&m->cia2,0x0fu,0x08u);
    cia6526_write(&m->cia1,0x03u,0x00u);
    cia6526_write(&m->cia2,0x03u,0x00u);
    if(!bt_sid6581_write_at(&m->sid,m->scheduler.cycle,0x18u,0x00u))return false;
    cia6526_write(&m->cia1,0x02u,0xffu);
    cia6526_write(&m->cia2,0x00u,0x07u);
    cia6526_write(&m->cia2,0x02u,0x3fu);
    c64_bus_cpu_write(&m->bus,0x0001u,0xe7u);
    c64_bus_cpu_write(&m->bus,0x0000u,0x2fu);
    /* The pinned PAL KERNAL continues at $FDDD/$FF6E: record PAL, program
       CIA1 Timer A to $4025 (period $4026 cycles), enable Timer-A IRQ and
       start/force-load it.  RESTOR also installs the $FD30 RAM vectors. */
    c64_bus_cpu_write(&m->bus,0x02a6u,BT_KERNAL_PAL_FLAG);
    cia6526_write(&m->cia1,0x04u,(uint8_t)BT_KERNAL_PAL_TIMER_A_LATCH);
    cia6526_write(&m->cia1,0x05u,(uint8_t)(BT_KERNAL_PAL_TIMER_A_LATCH>>8));
    cia6526_write(&m->cia1,0x0du,0x81u);
    cia6526_write(&m->cia1,0x0eu,0x11u);
    for(unsigned i=0;i<BT_KERNAL_DEFAULT_VECTOR_COUNT;i++)
        c64_bus_cpu_write(&m->bus,(uint16_t)(0x0314u+i),bt_kernal_default_vectors[i]);
    bt_c64_machine_recompute_interrupts(m);
    return !m->failed;
}

/* Reproduce only the persistent display state that the pinned KERNAL has
   established before BASIC receives control.  The generated constants are
   extracted fail-closed from KERNAL reset/RAMTAS/CINT/clear-line bytes by
   ProjectTools/natural_repair_kernal.py.  No oracle framebuffer bytes are
   involved.  Writes deliberately use the C64 bus so VIC-II and colour-RAM
   semantics remain owned by the hardware model. */
static bool apply_kernal_cold_display(bt_static_core *c){
    bt_c64_machine *m; unsigned i;
    if(!c)return false;
    m=&c->machine;
    /* RAMTAS/CINT persistent editor state relevant to the visible screen. */
    c64_bus_cpu_write(&m->bus,0x0282u,0x08u);
    c64_bus_cpu_write(&m->bus,0x0288u,(uint8_t)(BT_KERNAL_COLD_SCREEN_BASE>>8));
    c64_bus_cpu_write(&m->bus,0x0286u,BT_KERNAL_COLD_TEXT_COLOR);
    c64_bus_cpu_write(&m->bus,0x028bu,(uint8_t)(BT_KERNAL_COLD_SCREEN_BASE>>8));
    /* Remaining CINT keyboard/editor defaults that survive into normal IRQ
       operation.  The key decode vector points at the pinned KERNAL shift
       logic; the semantic scanner below uses the exact generated ROM tables. */
    c64_bus_cpu_write(&m->bus,0x0291u,0x00u);
    c64_bus_cpu_write(&m->bus,0x028fu,0x48u);
    c64_bus_cpu_write(&m->bus,0x0290u,0xebu);
    c64_bus_cpu_write(&m->bus,0x0289u,BT_KERNAL_KEYBUF_DEFAULT_MAX);
    c64_bus_cpu_write(&m->bus,0x028au,0x00u);
    c64_bus_cpu_write(&m->bus,0x028bu,BT_KERNAL_KEY_REPEAT_SPEED);
    c64_bus_cpu_write(&m->bus,0x028cu,BT_KERNAL_KEY_REPEAT_DELAY);
    c64_bus_cpu_write(&m->bus,0x028du,0x00u);
    c64_bus_cpu_write(&m->bus,0x028eu,0x00u);
    c64_bus_cpu_write(&m->bus,0x00c5u,BT_KERNAL_KEYCODE_NONE);
    c64_bus_cpu_write(&m->bus,0x00c6u,0x00u);
    c64_bus_cpu_write(&m->bus,0x00cbu,BT_KERNAL_KEYCODE_NONE);
    c64_bus_cpu_write(&m->bus,0x0099u,0x00u); /* keyboard input channel */
    c64_bus_cpu_write(&m->bus,0x009au,0x03u); /* screen output channel */
    /* CINT copies $ECB9-$ECE7 to $D000-$D02E. */
    for(i=0;i<BT_KERNAL_COLD_VIC_REG_COUNT;i++)
        c64_bus_cpu_write(&m->bus,(uint16_t)(0xd000u+i),bt_kernal_cold_vic_regs[i]);
    /* CINT clears 25 rows of 40 cells to screen-code space and current colour. */
    for(i=0;i<BT_KERNAL_COLD_SCREEN_CELLS;i++){
        c64_bus_cpu_write(&m->bus,(uint16_t)(BT_KERNAL_COLD_SCREEN_BASE+i),BT_KERNAL_COLD_SCREEN_CODE);
        c64_bus_cpu_write(&m->bus,(uint16_t)(0xd800u+i),BT_KERNAL_COLD_TEXT_COLOR);
    }
    bt_c64_machine_recompute_interrupts(m);
    return !m->failed;
}

static bool exact_static_load(bt_static_core *c,const char *name,bool relocate,uint16_t requested_start,uint16_t *load_addr,uint16_t *end_addr,uint32_t *identity_key){
    bt_d64_file f; bt_media_status st=BT_MEDIA_OK; uint8_t file[BT_KERNAL_FILE_BUFFER]; size_t used=0; uint16_t start,end; uint8_t d[32]; char hex[65]; uint32_t k;
    bt_d64_image *img;
    if(!c||!name||!c->drive.inserted||c->drive.active_side<1u||c->drive.active_side>2u)return false;
    img=&c->drive.sides[c->drive.active_side-1u];
    if(!bt_d64_find_file(img,name,&f,&st)||!bt_d64_read_file(img,&f,file,sizeof(file),&used,&st)||used<2u)return false;
    start=relocate?requested_start:(uint16_t)(file[0]|((uint16_t)file[1]<<8));
    if(used-2u>65536u-(size_t)start)return false;
    end=(uint16_t)(start+(uint16_t)(used-2u));
    bt_sha256(file+2u,used-2u,d); digest_hex(d,hex);
    for(k=1u;k<=bt_c06_identity_count();k++){
        const bt_c06_identity_meta *m=bt_c06_identity_meta_at(k);
        size_t expect;
        if(!m||m->processor_domain!=BT_C06_PROCESSOR_C64_6510)continue;
        expect=(size_t)m->range_end-(size_t)m->range_start+1u;
        if(m->range_start!=start||expect!=used-2u||strcmp(m->byte_hash,hex)!=0)continue;
        if(!bt_c06_registry_begin_identity_load(&c->registry,k))return false;
        memcpy(c->machine.bus.ram+start,file+2u,used-2u);
        if(!bt_c06_registry_commit_identity(&c->registry,k,hex))return false;
        if(load_addr)*load_addr=start;
        if(end_addr)*end_addr=end;
        if(identity_key)*identity_key=k;
        bt_static_emit_trace(c,BT_STATIC_TRACE_MEDIA,k,start,0x4c4f4144u,(uint64_t)(used-2u)); /* LOAD */
        return true;
    }
    return false; /* file not one of the sealed executable identities */
}

static bool semantic_rts(bt_static_core *c){
    cpu6510 *cpu; bt_c06_registry *r; bt_c06_shadow_frame *f; uint8_t lo,hi; uint16_t ret;
    if(!c)return false;
    cpu=&c->machine.cpu;
    r=&c->registry;
    if(r->c64_shadow_depth==0u){bt_static_emit_trace(c,BT_STATIC_TRACE_STOP,0u,cpu->pc,0x52545301u,cpu->sp);return false;}
    f=&r->c64_shadow[r->c64_shadow_depth-1u];
    if(f->kind!=(uint8_t)BT_AOT_SHADOW_CALL||f->processor_domain!=BT_C06_PROCESSOR_C64_6510||r->generation[f->identity_key]!=f->generation||r->c64_view!=f->c64_view){bt_static_emit_trace(c,BT_STATIC_TRACE_STOP,f->identity_key,cpu->pc,0x52545302u,((uint64_t)f->kind<<56)|((uint64_t)f->processor_domain<<48)|((uint64_t)r->c64_view<<40)|((uint64_t)f->c64_view<<32)|r->c64_shadow_depth);return false;}
    cpu->sp++; lo=c64_bus_cpu_peek(&c->machine.bus,(uint16_t)(0x0100u|cpu->sp));
    cpu->sp++; hi=c64_bus_cpu_peek(&c->machine.bus,(uint16_t)(0x0100u|cpu->sp));
    ret=(uint16_t)(((uint16_t)hi<<8)|lo); ret=(uint16_t)(ret+1u);
    if(ret!=f->return_pc||r->c64_owner[ret]!=f->identity_key){bt_static_emit_trace(c,BT_STATIC_TRACE_STOP,f->identity_key,cpu->pc,0x52545303u,((uint64_t)ret<<32)|((uint64_t)f->return_pc<<16)|r->c64_owner[ret]);return false;}
    r->c64_shadow_depth--; cpu->pc=ret; return true;
}

static bool advance_service(bt_static_core *c,uint32_t cycles){
    uint64_t target;
    if(!c)return false;
    target=c->machine.cpu.cycles+(uint64_t)cycles;
    if(!bt_c64_machine_aot_rendezvous(&c->machine,target)){bt_static_emit_trace(c,BT_STATIC_TRACE_STOP,0u,c->machine.cpu.pc,0x41445646u,((uint64_t)bt_1541_target_last_status(&c->drive)<<32)|c->drive.cpu.pc);return false;}
    c->machine.cpu.cycles=target; return true;
}

bool bt_kernal_target_cold_boot(bt_static_core *c){
    uint16_t start=0,end=0; uint32_t key=0;
    if(!c)return false;
    bt_kernal_target_state_init(&c->kernal_target);
    if(!apply_kernal_ioinit(c))return false;
    if(!apply_kernal_cold_display(c))return false;
    /* The pinned KERNAL reset is modelled through the real 6510 reset bus cycles.
       The target-specific bootstrap then performs only the statically proved first
       LOAD: exact Side-1 file BATTLETECH, whose payload is sealed C06 identity 1. */
    if(!exact_static_load(c,"BATTLETECH",false,0u,&start,&end,&key)||key!=1u||start!=0x02d4u)return false;
    c->machine.cpu.pc=start;
    /* The real autostart path reaches the loaded program with maskable IRQs
       enabled (oracle at BattleTech $930E has P.I=0).  cpu6510_reset itself
       correctly starts with I=1; the bounded KERNAL bootstrap must project
       the later CLI before handing control to the sealed program. */
    c->machine.cpu.p&=(uint8_t)~CPU6510_P_I;
    c->machine.cpu.irq_sampled=false;
    c->kernal_target.bootstrapped=1u;
    bt_static_emit_trace(c,BT_STATIC_TRACE_MEDIA,key,start,0x424f4f54u,end); /* BOOT */
    return true;
}


bool bt_battletech_drive_upload_service(bt_static_core *c,uint16_t entry){
    const bt_c06_identity_meta *owner,*id; const bt_drive_upload_record *r=NULL; uint16_t source,target; uint8_t buf[224],dig[32]; char hex[65]; size_t i,j;
    if(!c)return false;
    source=(uint16_t)(c64_bus_cpu_peek(&c->machine.bus,0x00fbu)|((uint16_t)c64_bus_cpu_peek(&c->machine.bus,0x00fcu)<<8));
    target=(uint16_t)(c->machine.cpu.y|((uint16_t)c->machine.cpu.x<<8));
    for(i=0;i<BT_DRIVE_UPLOAD_RECORD_COUNT;i++){
        const bt_drive_upload_record *q=&bt_drive_upload_records[i];
        if(q->entry==entry&&q->owner_identity_key==c->registry.c64_owner[entry]&&q->source==source&&q->target==target&&q->blocks==c->machine.cpu.a){r=q;break;}
    }
    if(!r||r->bytes>sizeof(buf))return false;
    owner=bt_c06_identity_meta_at(r->owner_identity_key);
    if(!owner||owner->processor_domain!=BT_C06_PROCESSOR_C64_6510||strcmp(owner->byte_hash,r->owner_sha256)!=0)return false;
    for(j=0;j<r->bytes;j++)buf[j]=c64_bus_cpu_peek(&c->machine.bus,(uint16_t)(r->source+(uint16_t)j));
    bt_sha256(buf,r->bytes,dig);digest_hex(dig,hex);if(strcmp(hex,r->sha256)!=0)return false;
    /* Replacing a drive-RAM generation must invalidate the prior static owner first.
       WESTWOOD deliberately installs a second proprietary loader generation; it is
       semantically isolated here and therefore does not fabricate a new AOT identity. */
    if(r->replace_identity_key&&!bt_c06_registry_begin_identity_load(&c->registry,r->replace_identity_key))return false;
    /* WESTWOOD installs a second proprietary 1541 loader generation. C04 proves the two upload callsites and immutable source ranges, while the generated fastloader service owns all subsequent file semantics. Enter semantic-loader mode before copying the first replacement so advance_service cannot execute an unadmitted drive generation. */
    if(r->owner_identity_key==3u)bt_1541_target_set_semantic_fastloader_mode(&c->drive,true);
    memcpy(c->drive.ram+r->target,buf,r->bytes);
    if(r->commit_identity_key){
        id=bt_c06_identity_meta_at(r->commit_identity_key);
        if(!id||id->processor_domain!=BT_C06_PROCESSOR_DRIVE_6502||id->range_start!=r->target||((uint32_t)id->range_end-(uint32_t)id->range_start+1u)!=r->bytes||strcmp(id->byte_hash,r->sha256)!=0)return false;
        if(!bt_c06_registry_commit_identity(&c->registry,r->commit_identity_key,hex))return false;
    }
    if(!advance_service(c,18000u+(uint32_t)r->bytes*60u))return false;
    bt_static_emit_trace(c,BT_STATIC_TRACE_MEDIA,r->commit_identity_key,r->target,0x4d575550u,(uint64_t)r->bytes); /* MWUP */
    return semantic_rts(c);
}

static const bt_fastloader_record *fastloader_record(uint8_t owner,uint16_t entry,const char *name,uint16_t dest,unsigned active_side){
    size_t i;
    if(!name)return NULL;
    for(i=0;i<bt_fastloader_record_count;i++){
        const bt_fastloader_record *r=&bt_fastloader_records[i];
        if(r->owner_identity_key!=owner||r->entry!=entry||strcmp(r->name,name)!=0)continue;
        if(r->control_side)return r;
        if(r->side==active_side&&r->dest==dest)return r;
    }
    return NULL;
}

static bool identity_has_executable_overlap(uint32_t key,uint16_t start,size_t n){
    uint32_t i; uint32_t lo=(uint32_t)start,hi=lo+(uint32_t)n-1u;
    for(i=0u;i<bt_c06_block_count();i++){
        const bt_c06_block_meta *b=bt_c06_block_meta_at(i); uint32_t blo,bhi;
        if(!b||b->identity_key!=key||b->processor_domain!=BT_C06_PROCESSOR_C64_6510)continue;
        blo=(uint32_t)b->pc; bhi=blo+(uint32_t)b->length-1u;
        if(!(bhi<lo||blo>hi))return true;
    }
    return false;
}

static bool invalidate_code_for_data_write(bt_static_core *c,uint16_t start,size_t n){
    uint8_t seen[256]={0}; size_t i; uint32_t k;
    if(!c||n==0u||n>65536u-(size_t)start)return false;
    for(i=0;i<n;i++){
        k=c->registry.c64_owner[(uint16_t)(start+(uint16_t)i)];
        if(k&&k<sizeof(seen))seen[k]=1u;
    }
    /* C05 closes several overlays that replace data inside broad executable identities.
       Revoke an identity only when the immutable load actually overlaps a statically
       compiled instruction footprint.  If the written range contains no admitted
       instruction bytes, unrelated generated code in the same broad identity remains
       valid; any future execution in the overlay range still has no block and traps. */
    for(k=1u;k<sizeof(seen);k++)if(seen[k]&&identity_has_executable_overlap(k,start,n)&&!bt_c06_registry_begin_identity_load(&c->registry,k))return false;
    return true;
}

bool bt_battletech_fastloader_service(bt_static_core *c,uint16_t entry){
    const bt_c06_identity_meta *owner; const bt_fastloader_record *rec; bt_d64_file f; bt_media_status st=BT_MEDIA_OK;
    uint8_t file[BT_KERNAL_FILE_BUFFER],dig[32]; char hex[65],name[17]; size_t used=0,i; uint16_t dest,p; uint32_t key=0; uint8_t owner_key; bool writable_save=false;
    if(!c)return false;
    owner_key=(uint8_t)c->registry.c64_owner[entry];
    if(owner_key!=2u&&owner_key!=3u)return false;
    owner=bt_c06_identity_meta_at(owner_key);
    if(!owner||owner->processor_domain!=BT_C06_PROCESSOR_C64_6510)return false;
    p=(uint16_t)(c->machine.cpu.y|((uint16_t)c->machine.cpu.x<<8));
    for(i=0;i<16u;i++){uint8_t ch=c64_bus_cpu_peek(&c->machine.bus,(uint16_t)(p+i));if(ch==0u)break;name[i]=(char)ch;}
    if(i==16u)return false;
    name[i]=0;
    dest=(uint16_t)(c64_bus_cpu_peek(&c->machine.bus,0x00fbu)|((uint16_t)c64_bus_cpu_peek(&c->machine.bus,0x00fcu)<<8));
    rec=fastloader_record(owner_key,entry,name,dest,c->drive.active_side); if(!rec)return false;
    if(strcmp(owner->byte_hash,rec->owner_identity_key==2u?"c8c0b1ea19020a6fc20a75d8b6e1d3f010e3233e9e5e99d13c2b0510c82ec0ad":"f387b5ba698ac1d174cf69a65d552dada23a5ee00cac23977f857073eb83b74c")!=0)return false;
    if(rec->control_side){
        if(!bt_1541_target_select_side(&c->drive,rec->control_side))return false;
        set_carry(&c->machine.cpu,0);
        bt_static_emit_trace(c,BT_STATIC_TRACE_MEDIA,0u,entry,0x53494445u,(uint64_t)rec->control_side); /* SIDE */
        return advance_service(c,2000u)&&semantic_rts(c);
    }
    if(!c->drive.inserted||c->drive.active_side!=rec->side)return false;
    if(!bt_d64_find_file(&c->drive.sides[rec->side-1u],name,&f,&st)||!bt_d64_read_file(&c->drive.sides[rec->side-1u],&f,file,sizeof(file),&used,&st)||used<2u)return false;
    if(used-2u!=(size_t)rec->bytes)return false;
    writable_save=(rec->side==2u&&rec->dest==0x13ceu&&rec->bytes==1696u&&name[0]=='G'&&name[1]>='0'&&name[1]<='5'&&name[2]=='\0');
    bt_sha256(file+2u,used-2u,dig);digest_hex(dig,hex);
    /* G0..G5 are the only mutable fastloader records.  Their original hashes
       prove the shipped media, while later in-session loads may carry bytes
       written by the bounded KERNAL SAVE service above.  They are data-only at
       $13CE-$1A6D, so length/destination/slot/side are the execution guard; all
       executable records still require their sealed payload hash exactly. */
    if(!writable_save&&strcmp(hex,rec->sha256)!=0)return false;
    /* If the payload is a sealed executable generation, install/commit that identity.
       Otherwise invalidate any executable generation overlapped by this immutable data load. */
    for(key=1u;key<=bt_c06_identity_count();key++){
        const bt_c06_identity_meta *m=bt_c06_identity_meta_at(key); size_t expect;
        if(!m||m->processor_domain!=BT_C06_PROCESSOR_C64_6510||m->range_start!=dest)continue;
        expect=(size_t)m->range_end-(size_t)m->range_start+1u;
        if(expect==used-2u&&strcmp(m->byte_hash,hex)==0)break;
    }
    if(key<=bt_c06_identity_count()){
        if(!bt_c06_registry_begin_identity_load(&c->registry,key))return false;
        memcpy(c->machine.bus.ram+dest,file+2u,used-2u);
        if(!bt_c06_registry_commit_identity(&c->registry,key,hex))return false;
    }else{
        if(!invalidate_code_for_data_write(c,dest,used-2u))return false;
        memcpy(c->machine.bus.ram+dest,file+2u,used-2u);
    }
    set_carry(&c->machine.cpu,0);
    bt_static_emit_trace(c,BT_STATIC_TRACE_MEDIA,key<=bt_c06_identity_count()?key:0u,dest,0x464c4452u,(uint64_t)(used-2u)); /* FLDR */
    if(!advance_service(c,12000u+(uint32_t)(used-2u)*4u))return false;
    return semantic_rts(c);
}

static bool handle_setlfs(bt_static_core *c){c->kernal_target.lfn=c->machine.cpu.a;c->kernal_target.device=c->machine.cpu.x;c->kernal_target.secondary=c->machine.cpu.y;set_carry(&c->machine.cpu,0);return advance_service(c,8u)&&semantic_rts(c);}
static bool handle_setnam(bt_static_core *c){
    unsigned i,n=c->machine.cpu.a; uint16_t p=(uint16_t)(c->machine.cpu.x|((uint16_t)c->machine.cpu.y<<8));
    if(n>16u)return false;
    c->kernal_target.name_len=(uint8_t)n;
    memset(c->kernal_target.name,0,sizeof(c->kernal_target.name));
    for(i=0;i<n;i++)c->kernal_target.name[i]=c64_bus_cpu_peek(&c->machine.bus,(uint16_t)(p+i));
    set_carry(&c->machine.cpu,0);
    return advance_service(c,8u)&&semantic_rts(c);
}
static bool handle_load(bt_static_core *c){
    char name[17]; uint16_t start=0,end=0; unsigned i;
    if(c->kernal_target.device!=8u||c->kernal_target.name_len==0u||c->kernal_target.name_len>16u)return false;
    for(i=0;i<c->kernal_target.name_len;i++)name[i]=(char)c->kernal_target.name[i];
    name[c->kernal_target.name_len]=0;
    if(!exact_static_load(c,name,c->kernal_target.secondary==0u,(uint16_t)(c->machine.cpu.x|((uint16_t)c->machine.cpu.y<<8)),&start,&end,NULL)){c->kernal_target.status_90=0x04u;set_carry(&c->machine.cpu,1);return false;}
    c->kernal_target.status_90=0u; c->machine.bus.ram[0x0090u]=0u;
    c->machine.cpu.x=(uint8_t)end; c->machine.cpu.y=(uint8_t)(end>>8); set_carry(&c->machine.cpu,0);
    /* Semantic KERNAL LOAD is a bounded target service. Its internal ROM/IEC loop is
       not interpreted. Give the hardware timeline deterministic load time while the
       exact payload is installed from immutable target media. */
    return advance_service(c,20000u+(uint32_t)(end-start)*350u)&&semantic_rts(c);
}

/* BattleTech save-game I/O uses the normal KERNAL entry points, but only for a
   very small, statically proved protocol on disk side 2:

     OPEN 15,8,15,"S0:Gn"  -- scratch the selected slot
     SAVE "Gn",8,$13CE-$1A6D
     OPEN 1,8,15,""        -- read drive status

   G0..G5 are the six preallocated seven-block save slots shipped on side 2.
   Their final size is invariant (1698 PRG bytes: load address + 1696 bytes), so
   the semantic target can preserve the original file chain and write only the
   existing sectors through the D64 copy-on-write overlay.  No directory/BAM
   allocation is synthesized and no host filesystem is involved. */
static bool battletech_save_slot_name(const bt_kernal_target_state *k,unsigned *slot){
    if(!k||k->name_len!=2u||k->name[0]!=(uint8_t)'G'||k->name[1]<(uint8_t)'0'||k->name[1]>(uint8_t)'5')return false;
    if(slot)*slot=(unsigned)(k->name[1]-(uint8_t)'0');
    return true;
}
static bool battletech_scratch_slot_name(const bt_kernal_target_state *k,unsigned *slot){
    if(!k||k->name_len!=5u||k->name[0]!=(uint8_t)'S'||k->name[1]!=(uint8_t)'0'||k->name[2]!=(uint8_t)':'||k->name[3]!=(uint8_t)'G'||k->name[4]<(uint8_t)'0'||k->name[4]>(uint8_t)'5')return false;
    if(slot)*slot=(unsigned)(k->name[4]-(uint8_t)'0');
    return true;
}
static bool d64_overwrite_exact_chain(bt_d64_image *d,const bt_d64_file *f,const uint8_t *data,size_t size){
    uint8_t t,s,sec[BT_D64_SECTOR_SIZE]; size_t done=0u; unsigned guard=0u; bt_media_status st=BT_MEDIA_OK;
    if(!d||!f||!data)return false;
    t=f->start_track;s=f->start_sector;
    while(t){uint8_t nt,ns;size_t take;
        if(++guard>BT_D64_SECTORS||!bt_d64_read_sector(d,t,s,sec,&st))return false;
        nt=sec[0];ns=sec[1];take=nt?254u:(ns?((size_t)ns-1u):0u);
        if(done+take>size)return false;
        memcpy(sec+2u,data+done,take);
        if(!bt_d64_write_sector(d,t,s,sec,&st))return false;
        done+=take;t=nt;s=ns;
    }
    return done==size;
}
static bool handle_open(bt_static_core *c){
    bt_d64_file f;bt_media_status st=BT_MEDIA_OK;unsigned slot=0u;char n[3]={'G','0','\0'};
    if(!c||c->kernal_target.device!=8u||c->kernal_target.secondary!=15u||c->drive.active_side!=2u)return false;
    if(c->kernal_target.lfn==15u&&battletech_scratch_slot_name(&c->kernal_target,&slot)){
        n[1]=(char)('0'+slot);
        /* Validate that the preallocated target exists.  The scratch operation
           itself is deferred until the immediately following SAVE so the fixed
           sector chain remains available for an exact overwrite. */
        if(!bt_d64_find_file(&c->drive.sides[1],n,&f,&st)||f.blocks!=7u)return false;
        bt_static_emit_trace(c,BT_STATIC_TRACE_MEDIA,0u,0xffc0u,0x53435254u,(uint64_t)slot); /* SCRT */
    }else if(c->kernal_target.lfn==1u&&c->kernal_target.name_len==0u){
        bt_static_emit_trace(c,BT_STATIC_TRACE_MEDIA,0u,0xffc0u,0x53544154u,0u); /* STAT */
    }else return false;
    c->kernal_target.status_90=0u;c->machine.bus.ram[0x0090u]=0u;set_carry(&c->machine.cpu,0);
    return advance_service(c,1200u)&&semantic_rts(c);
}
static bool handle_close(bt_static_core *c){
    uint8_t lfn;if(!c)return false;lfn=c->machine.cpu.a;
    if(lfn!=15u&&lfn!=1u)return false;
    if(lfn==1u){c->kernal_target.current_input=0xffu;c->machine.bus.ram[0x0099u]=0u;}
    c->kernal_target.status_90=0u;c->machine.bus.ram[0x0090u]=0u;set_carry(&c->machine.cpu,0);
    return advance_service(c,400u)&&semantic_rts(c);
}
static bool handle_save(bt_static_core *c){
    enum { BT_SAVE_START=0x13CEu, BT_SAVE_BYTES=1696u, BT_SAVE_FILE_BYTES=1698u };
    uint16_t start,end;uint8_t zp,file[BT_SAVE_FILE_BYTES],old[BT_SAVE_FILE_BYTES];size_t used=0u;unsigned i,slot=0u;bt_d64_file f;bt_media_status st=BT_MEDIA_OK;char n[3]={'G','0','\0'};
    if(!c||c->kernal_target.device!=8u||c->drive.active_side!=2u||!battletech_save_slot_name(&c->kernal_target,&slot))return false;
    zp=c->machine.cpu.a;start=(uint16_t)(c64_bus_cpu_peek(&c->machine.bus,zp)|((uint16_t)c64_bus_cpu_peek(&c->machine.bus,(uint8_t)(zp+1u))<<8));
    end=(uint16_t)(c->machine.cpu.x|((uint16_t)c->machine.cpu.y<<8));
    if(zp!=0xfbu||start!=BT_SAVE_START||end!=(uint16_t)(BT_SAVE_START+BT_SAVE_BYTES))return false;
    n[1]=(char)('0'+slot);
    if(!bt_d64_find_file(&c->drive.sides[1],n,&f,&st)||f.blocks!=7u||!bt_d64_read_file(&c->drive.sides[1],&f,old,sizeof(old),&used,&st)||used!=BT_SAVE_FILE_BYTES||old[0]!=(uint8_t)BT_SAVE_START||old[1]!=(uint8_t)(BT_SAVE_START>>8))return false;
    file[0]=(uint8_t)start;file[1]=(uint8_t)(start>>8);
    for(i=0u;i<BT_SAVE_BYTES;i++)file[i+2u]=c64_bus_cpu_peek(&c->machine.bus,(uint16_t)(start+i));
    if(!d64_overwrite_exact_chain(&c->drive.sides[1],&f,file,sizeof(file)))return false;
    c->kernal_target.status_90=0u;c->machine.bus.ram[0x0090u]=0u;set_carry(&c->machine.cpu,0);
    bt_static_emit_trace(c,BT_STATIC_TRACE_MEDIA,0u,start,0x53415645u,((uint64_t)slot<<32)|BT_SAVE_BYTES); /* SAVE */
    if(!advance_service(c,20000u+BT_SAVE_BYTES*350u))return false;
    /* The pinned 901227-03 serial SAVE path enters the IEC byte sender at
       $ED40.  That routine returns through CLI at $EDAB (and its error exit
       also executes CLI at $EDB5), so a successful disk SAVE returns with
       maskable interrupts enabled even when the caller used SEI.  BattleTech
       relies on that exact side effect before its post-save jiffy wait at
       $25E1.  Project only the proven status-bit effect here; the pending
       CIA1 timer IRQ remains owned by the real machine model. */
    c->machine.cpu.p&=(uint8_t)~CPU6510_P_I;
    c->machine.cpu.irq_sampled=false;
    return semantic_rts(c);
}
static bool handle_chkin(bt_static_core *c){
    if(!c||c->machine.cpu.x!=1u||c->kernal_target.device!=8u||c->kernal_target.secondary!=15u||c->kernal_target.name_len!=0u)return false;
    c->kernal_target.current_input=1u;c->machine.bus.ram[0x0099u]=8u;c->kernal_target.status_90=0u;c->machine.bus.ram[0x0090u]=0u;set_carry(&c->machine.cpu,0);
    return advance_service(c,60u)&&semantic_rts(c);
}
static bool handle_chrin(bt_static_core *c){
    static const uint8_t ok_line[]={ '0','0',',',' ','O','K',',','0','0',',','0','0',0x0du };
    unsigned idx;if(!c||c->machine.bus.ram[0x0099u]!=8u||c->kernal_target.current_input<1u)return false;
    idx=(unsigned)c->kernal_target.current_input-1u;if(idx>=sizeof(ok_line))return false;
    c->machine.cpu.a=ok_line[idx];c->kernal_target.current_input=(uint8_t)(c->kernal_target.current_input+1u);
    c->kernal_target.status_90=0u;c->machine.bus.ram[0x0090u]=0u;
    return advance_service(c,90u)&&semantic_rts(c);
}
static bool handle_clall(bt_static_core *c){c->kernal_target.current_input=0xffu;c->kernal_target.current_output=0xffu;c->kernal_target.status_90=0u;c->machine.bus.ram[0x0090u]=0u;c->machine.bus.ram[0x0099u]=0u;c->machine.bus.ram[0x009au]=3u;set_carry(&c->machine.cpu,0);return advance_service(c,12u)&&semantic_rts(c);}

static bool kernal_matrix_pressed(const bt_static_core *c,uint8_t row,uint8_t col){
    uint64_t bit;if(!c||row>7u||col>7u)return false;bit=UINT64_C(1)<<(row*8u+col);return (c->machine.input.keyboard&bit)!=0u;
}
static bool kernal_modifier_position(uint8_t row,uint8_t col){
    return (row==7u&&col==1u)||(row==4u&&col==6u)||(row==5u&&col==7u)||(row==2u&&col==7u);
}
/* Bounded semantic projection of the pinned $EA87 SCNKEY path.  The physical
   host event has already been converted to the real C64 8x8 matrix by the
   public input API.  This routine derives matrix code as col*8+row, maintains
   the KERNAL current/previous key and modifier bytes, translates through the
   exact 901227-03 tables generated from ROM, and queues a new edge in KEYD.
   Key-repeat timing is intentionally left to a later widening; BattleTech's
   title/menu entry needs the non-repeating edge behavior proved here. */
static bool kernal_scan_keyboard(bt_static_core *c){
    uint8_t row,col,key=BT_KERNAL_KEYCODE_NONE,old,mod=0u,oldmod,count,max,pet=0xffu;
    const uint8_t *table=bt_kernal_key_table_unshifted;
    if(!c)return false;
    if(kernal_matrix_pressed(c,7u,1u)||kernal_matrix_pressed(c,4u,6u))mod|=0x01u;
    if(kernal_matrix_pressed(c,5u,7u))mod|=0x02u;
    if(kernal_matrix_pressed(c,2u,7u))mod|=0x04u;
    for(col=0u;col<8u;col++)for(row=0u;row<8u;row++){
        uint8_t code;if(!kernal_matrix_pressed(c,row,col)||kernal_modifier_position(row,col))continue;
        code=(uint8_t)(col*8u+row);if(key==BT_KERNAL_KEYCODE_NONE||code>key)key=code;
    }
    old=c64_bus_cpu_peek(&c->machine.bus,0x00cbu);
    oldmod=c64_bus_cpu_peek(&c->machine.bus,0x028du);
    c64_bus_cpu_write(&c->machine.bus,0x00c5u,old);
    c64_bus_cpu_write(&c->machine.bus,0x00cbu,key);
    c64_bus_cpu_write(&c->machine.bus,0x028eu,oldmod);
    c64_bus_cpu_write(&c->machine.bus,0x028du,mod);
    if(key>=BT_KERNAL_KEYCODE_NONE||old!=BT_KERNAL_KEYCODE_NONE)return true;
    if(mod&0x04u)table=bt_kernal_key_table_control;
    else if(mod&0x02u)table=bt_kernal_key_table_commodore;
    else if(mod&0x01u)table=bt_kernal_key_table_shifted;
    pet=table[key];if(pet==0xffu)return true;
    count=c64_bus_cpu_peek(&c->machine.bus,0x00c6u);
    max=c64_bus_cpu_peek(&c->machine.bus,0x0289u);if(max>10u)max=10u;
    if(count>=max)return true;
    c64_bus_cpu_write(&c->machine.bus,(uint16_t)(0x0277u+count),pet);
    c64_bus_cpu_write(&c->machine.bus,0x00c6u,(uint8_t)(count+1u));
    bt_static_emit_trace(c,BT_STATIC_TRACE_INPUT,0u,c->machine.cpu.pc,0x4b425546u,((uint64_t)key<<16)|((uint64_t)mod<<8)|pet); /* KBUF */
    return true;
}
static bool handle_scnkey(bt_static_core *c){return kernal_scan_keyboard(c)&&advance_service(c,620u)&&semantic_rts(c);}
static bool handle_chrout(bt_static_core *c){
    uint8_t ch,mode,device;
    if(!c)return false;
    ch=c->machine.cpu.a;
    device=c64_bus_cpu_peek(&c->machine.bus,0x009au);
    /* C06 contains one CHROUT call: WESTWOOD $24BC with A=$08 while
       the normal output device is the screen (3).  In the pinned 901227-03
       editor, PETSCII $08 sets bit 7 of MODE ($0291) to disable the
       SHIFT+C= character-set toggle; $09 clears it.  Handle only that exact
       persistent editor operation and fail closed for broader screen output. */
    if(device!=0x03u)return false;
    mode=c64_bus_cpu_peek(&c->machine.bus,0x0291u);
    if(ch==0x08u)mode|=0x80u;
    else if(ch==0x09u)mode&=0x7fu;
    else return false;
    c64_bus_cpu_write(&c->machine.bus,0x0291u,mode);
    bt_static_emit_trace(c,BT_STATIC_TRACE_C64_BLOCK,0u,0xffd2u,0x43484f55u,((uint64_t)device<<16)|((uint64_t)ch<<8)|mode); /* CHOU */
    return advance_service(c,32u)&&semantic_rts(c);
}

static bool handle_getin(bt_static_core *c){
    uint8_t count,i,a=0u;if(!c)return false;count=c64_bus_cpu_peek(&c->machine.bus,0x00c6u);
    if(count){a=c64_bus_cpu_peek(&c->machine.bus,0x0277u);for(i=1u;i<count;i++)c64_bus_cpu_write(&c->machine.bus,(uint16_t)(0x0276u+i),c64_bus_cpu_peek(&c->machine.bus,(uint16_t)(0x0277u+i)));c64_bus_cpu_write(&c->machine.bus,0x00c6u,(uint8_t)(count-1u));}
    c->machine.cpu.a=a;return advance_service(c,45u)&&semantic_rts(c);
}

static void stack_push_raw(bt_static_core *c,uint8_t v){c64_bus_cpu_write(&c->machine.bus,(uint16_t)(0x0100u|c->machine.cpu.sp),v);c->machine.cpu.sp--;}
static uint8_t stack_pop_raw(bt_static_core *c){c->machine.cpu.sp++;return c64_bus_cpu_peek(&c->machine.bus,(uint16_t)(0x0100u|c->machine.cpu.sp));}

/* Exact control effect of the pinned KERNAL hardware IRQ entry at $FF48.
   The CPU has already pushed PC/P in cpu6510_service_pending_interrupt(). KERNAL saves A/X/Y,
   then dispatches through CINV ($0314) for hardware IRQs. */
static bool handle_irq_dispatch(bt_static_core *c){
    uint16_t target; uint16_t key;
    if(!c)return false;
    stack_push_raw(c,c->machine.cpu.a); stack_push_raw(c,c->machine.cpu.x); stack_push_raw(c,c->machine.cpu.y);
    target=(uint16_t)(c64_bus_cpu_peek(&c->machine.bus,0x0314u)|((uint16_t)c64_bus_cpu_peek(&c->machine.bus,0x0315u)<<8));
    key=c->registry.c64_owner[target];
    if(target!=BT_KERNAL_DEFAULT_IRQ_VECTOR && (!key||bt_c06_find_block(key,target)<0))return false;
    if(!advance_service(c,12u))return false;
    c->machine.cpu.pc=target;
    bt_static_emit_trace(c,BT_STATIC_TRACE_C64_BLOCK,key,target,0x49525144u,0xff48u); /* IRQD */
    return true;
}

static bool finish_default_irq(bt_static_core *c){
    bt_c06_registry *r; bt_c06_shadow_frame *f; cpu6510 *cpu; uint8_t p,lo,hi; uint16_t ret;
    if(!c)return false;
    r=&c->registry;
    cpu=&c->machine.cpu;
    if(r->c64_shadow_depth==0u)return false;
    f=&r->c64_shadow[r->c64_shadow_depth-1u];
    if(f->kind!=(uint8_t)BT_AOT_SHADOW_INTERRUPT||f->processor_domain!=BT_C06_PROCESSOR_C64_6510||r->generation[f->identity_key]!=f->generation||r->c64_view!=f->c64_view)return false;
    /* $EA81 restores the A/X/Y saved by $FF48, then RTI restores P/PC. */
    cpu->y=stack_pop_raw(c); cpu->x=stack_pop_raw(c); cpu->a=stack_pop_raw(c);
    p=stack_pop_raw(c); lo=stack_pop_raw(c); hi=stack_pop_raw(c); ret=(uint16_t)(lo|((uint16_t)hi<<8));
    if(ret!=f->return_pc||r->c64_owner[ret]!=f->identity_key)return false;
    cpu->p=(uint8_t)((p|CPU6510_P_U)&(uint8_t)~CPU6510_P_B); cpu->pc=ret; cpu->irq_sampled=false; r->c64_shadow_depth--;
    bt_c64_machine_recompute_interrupts(&c->machine);
    return true;
}

/* Target-relevant semantic of the standard $EA31 IRQ service: UDTIM, SCNKEY
   and CIA1 acknowledgement.  Keyboard scanning consumes only the public C64
   matrix state and pinned KERNAL decode tables; it does not interpret host keys. */
static bool handle_default_irq(bt_static_core *c){
    uint8_t lo,mid,hi;
    if(!c)return false;
    lo=(uint8_t)(c64_bus_cpu_peek(&c->machine.bus,0x00a2u)+1u); c64_bus_cpu_write(&c->machine.bus,0x00a2u,lo);
    if(lo==0u){mid=(uint8_t)(c64_bus_cpu_peek(&c->machine.bus,0x00a1u)+1u);c64_bus_cpu_write(&c->machine.bus,0x00a1u,mid);if(mid==0u){hi=(uint8_t)(c64_bus_cpu_peek(&c->machine.bus,0x00a0u)+1u);c64_bus_cpu_write(&c->machine.bus,0x00a0u,hi);}}
    if(!kernal_scan_keyboard(c))return false;
    /* KERNAL reads $DC0D at $EA7E, clearing the Timer-A IRQ source. */
    (void)cia6526_read(&c->machine.cia1,0x0du); bt_c64_machine_recompute_interrupts(&c->machine);
    if(!advance_service(c,160u))return false;
    return finish_default_irq(c);
}

static bool handle_nmi_trampoline(bt_static_core *c){
    uint16_t target; uint16_t key;
    if(!c)return false;
    target=(uint16_t)(c64_bus_cpu_peek(&c->machine.bus,0x0318u)|((uint16_t)c64_bus_cpu_peek(&c->machine.bus,0x0319u)<<8));
    key=c->registry.c64_owner[target];
    if(!key||bt_c06_find_block(key,target)<0)return false;
    c->machine.cpu.p|=CPU6510_P_I;
    if(!advance_service(c,7u))return false;
    c->machine.cpu.pc=target;
    bt_static_emit_trace(c,BT_STATIC_TRACE_C64_BLOCK,key,target,0x4e4d4954u,0xfe43u);
    return true;
}


static bool validate_completed_game_rts(bt_static_core *c){
    bt_c06_registry *r=&c->registry; bt_c06_shadow_frame *f; uint16_t pc=c->machine.cpu.pc;
    if(r->c64_shadow_depth==0u){bt_static_emit_trace(c,BT_STATIC_TRACE_STOP,0u,pc,0x47525401u,0u);return false;}
    f=&r->c64_shadow[r->c64_shadow_depth-1u];
    if(f->kind!=(uint8_t)BT_AOT_SHADOW_CALL||f->processor_domain!=BT_C06_PROCESSOR_C64_6510||r->generation[f->identity_key]!=f->generation||r->c64_view!=f->c64_view||f->return_pc!=pc||r->c64_owner[pc]!=f->identity_key){bt_static_emit_trace(c,BT_STATIC_TRACE_STOP,f->identity_key,pc,0x47525402u,((uint64_t)f->return_pc<<48)|((uint64_t)f->kind<<40)|((uint64_t)f->processor_domain<<32)|((uint64_t)r->c64_view<<24)|((uint64_t)f->c64_view<<16)|r->c64_shadow_depth);return false;}
    r->c64_shadow_depth--; return true;
}
static bool kernal_static_step(bt_static_core *c,uint16_t pc){
    const bt_kernal_aot_meta *m=bt_kernal_aot_lookup(pc); cpu6510_step_result sr; uint16_t next; bool allowed=false;
    bool deferred_nmi;
    if(!c||!m||c64_bus_cpu_source(&c->machine.bus,pc)!=C64_BUS_SRC_KERNAL)return false;
    if(!bt_c64_machine_aot_rendezvous(&c->machine,c->machine.cpu.cycles))return false;
    /* System-ROM AOT is a bounded atomic boundary for asynchronous delivery.
       cpu6510_step_fixed intentionally rejects a pending interrupt at entry, so
       mark an asserted IRQ as already sampled only for this fixed instruction.
       It recomputes irq_sampled from P before executing, leaving the pending IRQ
       eligible as soon as the KERNAL path returns to game-owned code.  Preserve
       an NMI edge explicitly because edges, unlike IRQ level, must not be lost. */
    deferred_nmi=c->machine.cpu.nmi_edge_pending;
    if(deferred_nmi)c->machine.cpu.nmi_edge_pending=false;
    if(c->machine.cpu.irq_line&&!c->machine.cpu.irq_sampled)c->machine.cpu.irq_sampled=true;
    sr=cpu6510_step_fixed(&c->machine.cpu,pc,m->opcode);
    if(deferred_nmi)c->machine.cpu.nmi_edge_pending=true;
    if(sr.stop_reason!=CPU6510_STOP_NONE)return false;
    next=c->machine.cpu.pc;
    if(m->kind==1u){
        if(c->kernal_target.aot_call_depth>=32u||next!=m->succ0)return false;
        c->kernal_target.aot_call_return[c->kernal_target.aot_call_depth++]=m->return_pc;
    }else if(m->kind==2u){
        if(c->kernal_target.aot_call_depth){uint16_t ret=c->kernal_target.aot_call_return[--c->kernal_target.aot_call_depth];if(next!=ret)return false;}
        else if(!validate_completed_game_rts(c))return false;
    }else{
        if(m->succ_count==0u)return false;
        if(next==m->succ0||(m->succ_count>1u&&next==m->succ1))allowed=true;
        if(!allowed)return false;
    }
    bt_static_emit_trace(c,BT_STATIC_TRACE_C64_BLOCK,0u,pc,0x4b414f54u,m->opcode); /* KAOT */
    return true;
}

bool bt_kernal_target_dynamic_trampoline(bt_static_core *c,uint16_t from_pc,uint16_t target){
    uint16_t encoded,operand; uint32_t owner;
    if(!c||target!=BT_KERNAL_DEFAULT_IRQ_VECTOR)return false;
    /* BattleTech installs two source-proved IRQ chains during the Side-1 route.
       BOOT ($9694, identity 2; SMC0040/0041) and WESTWOOD ($3274, identity 3;
       SMC0577/0578) each save the pre-existing CINV target into the operands of
       an originally JMP $FFFF instruction before replacing $0314/$0315.  C05
       intentionally leaves the later execution trapped.  Admit only these two
       exact sealed trampolines, only while their owning identity is live, only
       when the runtime operands themselves encode the pinned default IRQ, and
       only while KERNAL ROM is visible at that destination. */
    if(from_pc==0x9694u){owner=2u;operand=0x9695u;}
    else if(from_pc==0x3274u){owner=3u;operand=0x3275u;}
    else return false;
    if(c->registry.c64_owner[from_pc]!=owner)return false;
    encoded=(uint16_t)(c64_bus_cpu_peek(&c->machine.bus,operand)|
                      ((uint16_t)c64_bus_cpu_peek(&c->machine.bus,(uint16_t)(operand+1u))<<8));
    if(encoded!=target||c64_bus_cpu_source(&c->machine.bus,target)!=C64_BUS_SRC_KERNAL)return false;
    bt_static_emit_trace(c,BT_STATIC_TRACE_C64_BLOCK,owner,from_pc,0x44594e54u,target); /* DYNT */
    return bt_kernal_target_service(c,target);
}

bool bt_kernal_target_service(bt_static_core *c,uint16_t entry){
    bool ok=false; if(!c)return false; c->kernal_target.service_count++;bt_static_emit_trace(c,BT_STATIC_TRACE_C64_BLOCK,0u,entry,0x4b454e54u,c->kernal_target.service_count);
    switch(entry){
      case 0xffbau: ok=handle_setlfs(c);break;
      case 0xffbdu: ok=handle_setnam(c);break;
      case 0xffc0u: ok=handle_open(c);break;
      case 0xffc3u: ok=handle_close(c);break;
      case 0xffc6u: ok=handle_chkin(c);break;
      case 0xffcfu: ok=handle_chrin(c);break;
      case 0xffd5u: ok=handle_load(c);break;
      case 0xffd8u: ok=handle_save(c);break;
      case 0xffe7u: ok=handle_clall(c);break;
      case 0xff9fu: ok=handle_scnkey(c);break;
      case 0xffe4u: ok=handle_getin(c);break;
      case 0xffd2u: ok=handle_chrout(c);break;
      case 0xff48u: ok=handle_irq_dispatch(c);break;
      case 0xea31u: ok=handle_default_irq(c);break;
      case 0xfe43u: ok=handle_nmi_trampoline(c);break;
      default: if(bt_kernal_aot_lookup(entry))ok=kernal_static_step(c,entry);else return false;break;
    }
    if(ok)bt_static_emit_trace(c,BT_STATIC_TRACE_C64_BLOCK,0u,entry,0x4b45524eu,c->kernal_target.service_count); /* KERN */
    return ok;
}
