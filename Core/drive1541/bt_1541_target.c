#include "bt_1541_target.h"
#include "bt_gcr.h"
#include "../media/bt_sha256.h"
#include "../../Generated/C06/Source/aot_catalog.h"
#include "../../Generated/NaturalRepair/Drive0400/bt_drive0400_activation_generated.h"
#include <string.h>

#define BT_DRIVE_ROM_SHA256 "326c289c38753323d7e8167897447cf61ef35189d82eb8d75210ece949adda7c"

static void fail(bt_1541_target *d,bt_1541_target_status s){if(d){d->failed=1u;d->status=s;}}
static bool hash_ok(const uint8_t *p,size_t n,const char *expected){uint8_t h[32];bt_sha256(p,n,h);return bt_sha256_equal_hex(h,expected);}
static bool ram_address(uint16_t a){return (a&0x1800u)==0u && a<0x8000u;}
static bool via_iec_address(uint16_t a){return (a&0x1c00u)==0x1800u && a<0x8000u;}
static bool via_disk_address(uint16_t a){return (a&0x1c00u)==0x1c00u && a<0x8000u;}
static uint16_t ram_index(uint16_t a){return (uint16_t)(a&0x07ffu);}

static void refresh_irq(bt_1541_target *d){cpu6510_set_irq(&d->cpu,bt_via6522_irq(&d->via_iec)||bt_via6522_irq(&d->via_disk));}
static void tick_device_to(bt_1541_target *d,uint64_t cyc);

static uint8_t via_iec_read_port(void *ctx,unsigned port,uint8_t latch,uint8_t ddr){
 bt_1541_target*d=(bt_1541_target*)ctx;(void)latch;(void)ddr;
 if(port==0u)return bt_iec_drive_via1_pb_inputs(d->iec,BT_1541_TARGET_DEVICE);
 return 0xffu;
}
static void via_iec_write_port(void *ctx,unsigned port,uint8_t latch,uint8_t ddr,uint8_t driven){
 bt_1541_target*d=(bt_1541_target*)ctx;(void)driven;
 if(port==0u){uint8_t p=(uint8_t)(latch&ddr);bt_iec_set_drive_outputs(d->iec,(p&0x02u)!=0u,(p&0x08u)!=0u,(p&0x10u)!=0u);}
}
static void via_iec_control(void *ctx,unsigned pin,bool level){(void)ctx;(void)pin;(void)level;/* Parallel-cable control is outside the target profile. */}

static unsigned current_track(const bt_1541_target*d){unsigned h=d->half_track;if(h<2u)h=2u;if(h>70u)h=70u;return h/2u;}
static void load_track(bt_1541_target*d){
 uint8_t sectors[21u*256u],bam[256];unsigned t=current_track(d),sc=bt_d64_sectors_on_track(t),s;size_t used=0;bt_media_status st;uint8_t id1=0,id2=0;
 d->gcr_track_bytes=0;d->media_byte_index=0;d->disk_byte=0xffu;d->sync_level=1u;d->sync_ff_run=0u;
 if(!d->inserted||d->active_side<1u||d->active_side>2u)return;
 if(!bt_d64_read_sector(&d->sides[d->active_side-1u],18u,0u,bam,&st))return;
 id1=bam[0xa2u];id2=bam[0xa3u];
 for(s=0;s<sc;s++)if(!bt_d64_read_sector(&d->sides[d->active_side-1u],t,s,sectors+(size_t)s*256u,&st))return;
 if(bt_gcr_build_track((uint8_t)t,id1,id2,sectors,(size_t)sc*256u,d->gcr_track,sizeof(d->gcr_track),&used))d->gcr_track_bytes=used;
}
static void set_stepper(bt_1541_target*d,uint8_t phase){
 static const int8_t delta[4][4]={{0,1,0,-1},{-1,0,1,0},{0,-1,0,1},{1,0,-1,0}};
 uint8_t old=d->stepper_phase&3u,n=phase&3u;int v=delta[old][n];d->stepper_phase=n;
 if(v<0&&d->half_track>2u)d->half_track--;else if(v>0&&d->half_track<70u)d->half_track++;
 if(v)load_track(d);
}
static uint8_t via_disk_read_port(void *ctx,unsigned port,uint8_t latch,uint8_t ddr){
 bt_1541_target*d=(bt_1541_target*)ctx;(void)latch;(void)ddr;
 if(port==1u)return d->disk_byte;
 {uint8_t v=0u;if(d->write_protect_level)v|=0x10u;if(d->sync_level)v|=0x80u;return v;}
}
static void via_disk_write_port(void *ctx,unsigned port,uint8_t latch,uint8_t ddr,uint8_t driven){
 bt_1541_target*d=(bt_1541_target*)ctx;(void)driven;
 if(port==1u){d->disk_write_byte=(uint8_t)(latch&ddr);return;}
 {uint8_t p=(uint8_t)(latch&ddr);set_stepper(d,(uint8_t)(p&3u));d->motor_on=(uint8_t)((p&0x04u)!=0u);d->activity_led=(uint8_t)((p&0x08u)!=0u);d->density_zone=(uint8_t)((p>>5)&3u);}
}
static void flush_track_cow(bt_1541_target*d){
 uint8_t decoded[21u*256u],old[256];uint32_t mask=0;unsigned t=current_track(d),s,sc=bt_d64_sectors_on_track(t);bt_media_status st;
 if(!d->inserted||d->active_side<1u||d->active_side>2u||!d->gcr_track_bytes)return;
 if(!bt_gcr_extract_track((uint8_t)t,d->gcr_track,d->gcr_track_bytes,decoded,sizeof(decoded),&mask)){fail(d,BT_1541_TARGET_UNSUPPORTED_DEVICE_STATE);return;}
 for(s=0;s<sc;s++)if(mask&((uint32_t)1u<<s)){
  if(!bt_d64_read_sector(&d->sides[d->active_side-1u],t,s,old,&st)){fail(d,BT_1541_TARGET_BAD_MEDIA);return;}
  if(memcmp(old,decoded+(size_t)s*256u,256u)!=0&&!bt_d64_write_sector(&d->sides[d->active_side-1u],t,s,decoded+(size_t)s*256u,&st)){fail(d,BT_1541_TARGET_BAD_MEDIA);return;}
 }
}
static void via_disk_control(void *ctx,unsigned pin,bool level){
 bt_1541_target*d=(bt_1541_target*)ctx;
 if(pin==0u){/* 6502 SO is active-low; PCR output-low enables the byte-ready SO path. */d->disk_set_overflow_enable=(uint8_t)!level;}
 else{/* 64H156 OE is active-low. Flush valid changed sector records when write mode is released. */uint8_t old=d->disk_output_enable;d->disk_output_enable=(uint8_t)!level;if(old&&!d->disk_output_enable)flush_track_cow(d);}
}

static void media_tick(bt_1541_target*d){
 static const uint8_t byte_cycles[4]={32u,30u,28u,26u};uint8_t period=byte_cycles[d->density_zone&3u];
 if(!d->motor_on||!d->inserted||d->gcr_track_bytes==0u)return;
 d->media_cycle_accumulator++;
 if(d->media_cycle_accumulator<period)return;
 d->media_cycle_accumulator-=period;
 if(d->disk_output_enable){
  /* Target media writes are retained as sector-level COW. Raw track writes that cannot
     be mapped back to an admitted D64 sector fail closed instead of creating G64 state. */
  if(d->write_protect_level){fail(d,BT_1541_TARGET_UNSUPPORTED_DEVICE_STATE);return;}
  d->gcr_track[d->media_byte_index%d->gcr_track_bytes]=d->disk_write_byte;
 }else d->disk_byte=d->gcr_track[d->media_byte_index%d->gcr_track_bytes];
 d->media_byte_index=(d->media_byte_index+1u)%d->gcr_track_bytes;
 if(d->disk_byte==0xffu){if(d->sync_ff_run<2u)d->sync_ff_run++;}else d->sync_ff_run=0u;d->sync_level=(uint8_t)(d->sync_ff_run>=2u?0u:1u);
 d->byte_ready_level^=1u;bt_via6522_set_ca1(&d->via_disk,d->byte_ready_level!=0u);
 if(d->disk_set_overflow_enable)d->cpu.p|=CPU6510_P_V;
}
static void tick_device_to(bt_1541_target*d,uint64_t cyc){
 while(d->via_iec.cycle<cyc&&!d->failed){uint64_t n=d->via_iec.cycle+1u;bt_via6522_set_ca1(&d->via_iec,!bt_iec_atn_high(d->iec));bt_via6522_tick(&d->via_iec,n);bt_via6522_tick(&d->via_disk,n);media_tick(d);refresh_irq(d);}
}

static void bus_begin(void *ctx,uint64_t cyc,uint16_t addr,cpu6510_bus_kind kind){bt_1541_target*d=(bt_1541_target*)ctx;(void)addr;(void)kind;tick_device_to(d,cyc);}
static uint8_t bus_read(void *ctx,uint16_t a,cpu6510_bus_kind kind){bt_1541_target*d=(bt_1541_target*)ctx;(void)kind;if(ram_address(a))return d->ram[ram_index(a)];if(via_iec_address(a))return bt_via6522_read(&d->via_iec,(uint8_t)a);if(via_disk_address(a))return bt_via6522_read(&d->via_disk,(uint8_t)a);if(a>=0xc000u&&d->rom)return d->rom[a-0xc000u];return 0xffu;}
static bool drive0400_workspace(uint16_t a){return a>=BT_DRIVE0400_MUTABLE_LO&&a<=BT_DRIVE0400_MUTABLE_HI;}
static bool drive0400_external_source(uint16_t pc){return pc==0x0503u||pc==0x0506u||pc==0xcdbau||pc==0xf379u;}

static bool activate_drive0400_live_identity(bt_1541_target*d){
 const bt_c06_identity_meta*m;uint8_t h[32];size_t i;int32_t blocked;
 if(!d||!d->registry)return false;
 m=bt_c06_identity_meta_at(BT_DRIVE0400_IDENTITY_KEY);if(!m)return false;
 /* C05/C06 sealed authority admits only $0400/$0404 external entries.  The
    generated repair proof establishes $0407-$0417 as unreachable workspace.
    Guard every reachable generated instruction against its sealed C06
    instruction hash before activating the identity. */
 for(i=0;i<bt_drive0400_guard_count;i++){
  const bt_drive0400_guard*g=&bt_drive0400_guards[i];
  if(g->pc<m->range_start||((uint32_t)g->pc+g->length-1u)>m->range_end)return false;
  bt_sha256(d->ram+g->pc,g->length,h);
  if(!bt_sha256_equal_hex(h,g->instruction_sha256))return false;
 }
 if(!bt_c06_registry_commit_identity(d->registry,BT_DRIVE0400_IDENTITY_KEY,m->byte_hash))return false;
 blocked=bt_c06_find_block(BT_DRIVE0400_IDENTITY_KEY,BT_DRIVE0400_BLOCKED_PC);
 if(blocked<0)return false;
 d->registry->blocked[(uint32_t)blocked]=1u;
 return true;
}

static void maybe_commit_ram_identity(bt_1541_target*d,uint32_t key,uint16_t lo,uint16_t hi){
 const bt_c06_identity_meta*m=bt_c06_identity_meta_at(key);uint8_t h[32];char hex[65];unsigned i;
 if(!m||lo!=m->range_start||hi!=m->range_end)return;
 bt_sha256(d->ram+lo,(size_t)(hi-lo+1u),h);for(i=0;i<32u;i++){static const char x[]="0123456789abcdef";hex[i*2u]=x[h[i]>>4];hex[i*2u+1u]=x[h[i]&15u];}hex[64]=0;
 if(strcmp(hex,m->byte_hash)==0&&!bt_c06_registry_commit_identity(d->registry,key,hex))fail(d,BT_1541_TARGET_REGISTRY_FAILURE);
}
static void ram_write_registry(bt_1541_target*d,uint16_t a){
 struct rr{uint32_t key;uint16_t lo,hi;}r[]={{19u,0x0300u,0x0377u},{18u,0x0400u,0x049fu},{17u,0x0500u,0x05dfu}};unsigned i;uint16_t owner=d->registry?d->registry->drive_owner[a]:0u;
 if(!d->registry)return;
 if(drive0400_workspace(a))return; /* sealed static proof: unreachable mutable installer workspace */
 if(owner&&owner==d->executing_identity_key){if(!bt_c06_registry_apply_smc_write(d->registry,d->executing_identity_key,d->executing_pc,a))fail(d,BT_1541_TARGET_REGISTRY_FAILURE);return;}
 for(i=0;i<sizeof(r)/sizeof(r[0]);i++)if(a>=r[i].lo&&a<=r[i].hi){if(owner&&!bt_c06_registry_begin_identity_load(d->registry,r[i].key)){fail(d,BT_1541_TARGET_REGISTRY_FAILURE);return;}maybe_commit_ram_identity(d,r[i].key,r[i].lo,r[i].hi);return;}
}
static void bus_write(void *ctx,uint16_t a,uint8_t x,cpu6510_bus_kind kind){bt_1541_target*d=(bt_1541_target*)ctx;(void)kind;if(ram_address(a)){uint16_t ri=ram_index(a);d->ram[ri]=x;ram_write_registry(d,ri);return;}if(via_iec_address(a)){bt_via6522_write(&d->via_iec,(uint8_t)a,x);if(d->via_iec.unsupported)fail(d,BT_1541_TARGET_UNSUPPORTED_DEVICE_STATE);refresh_irq(d);return;}if(via_disk_address(a)){bt_via6522_write(&d->via_disk,(uint8_t)a,x);if(d->via_disk.unsupported)fail(d,BT_1541_TARGET_UNSUPPORTED_DEVICE_STATE);refresh_irq(d);return;}if(a>=0xc000u)fail(d,BT_1541_TARGET_UNSUPPORTED_DEVICE_STATE);}
static bool aot_rendezvous(void *ctx,uint64_t cyc){bt_1541_target*d=(bt_1541_target*)ctx;tick_device_to(d,cyc);refresh_irq(d);return d&&!d->failed;}

/*
 * The drive must never execute a bus cycle from the future, but delaying every
 * instruction until eight cycles are available creates an artificial 0-7 cycle
 * lag.  BattleTech's M-W uploader is sensitive to that lag.  Predict the exact
 * cycle count of the next already-admitted static instruction using a cloned CPU
 * and a side-effect-free drive memory view, then execute the real instruction
 * only when its complete cycle span is inside the current drive target.  This is
 * scheduling only: opcode selection still comes exclusively from the sealed C06
 * static block catalogue.
 */
static void predict_begin(void *ctx,uint64_t cyc,uint16_t addr,cpu6510_bus_kind kind){(void)ctx;(void)cyc;(void)addr;(void)kind;}
static uint8_t predict_read(void *ctx,uint16_t a,cpu6510_bus_kind kind){
 bt_1541_target*d=(bt_1541_target*)ctx;(void)kind;
 if(ram_address(a))return d->ram[ram_index(a)];
 if(a>=0xc000u&&d->rom)return d->rom[a-0xc000u];
 /* VIA/data values cannot change the current instruction's cycle count.
    Zero-page indirect pointers are drive RAM and are handled above. */
 return 0xffu;
}
static void predict_write(void *ctx,uint16_t a,uint8_t v,cpu6510_bus_kind kind){(void)ctx;(void)a;(void)v;(void)kind;}
static uint32_t predict_next_cycles(bt_1541_target*d){
 cpu6510 p;cpu6510_bus b;cpu6510_step_result ir;uint16_t k;int32_t bi;const bt_c06_block_meta*m;
 if(!d||d->failed||!d->registry)return 0u;
 p=d->cpu;b.ctx=d;b.begin_cycle=predict_begin;b.read=predict_read;b.write=predict_write;p.bus=b;
 if(p.nmi_edge_pending||(p.irq_line&&!p.irq_sampled)){ir=cpu6510_service_pending_interrupt(&p);return ir.cycles;}
 k=d->registry->drive_owner[p.pc];if(!k)return 0u;bi=bt_c06_find_block(k,p.pc);if(bi<0)return 0u;m=bt_c06_block_meta_at((uint32_t)bi);if(!m)return 0u;
 ir=cpu6510_step_fixed(&p,m->pc,m->opcode);if(ir.stop_reason!=CPU6510_STOP_NONE)return 0u;return ir.cycles;
}

bool bt_1541_target_init(bt_1541_target*d,bt_c06_registry*r,bt_iec_bus*iec,const uint8_t*rom,size_t n){cpu6510_bus b;if(!d||!r||!iec||!rom||n!=BT_1541_TARGET_ROM_BYTES)return false;if(!hash_ok(rom,n,BT_DRIVE_ROM_SHA256))return false;memset(d,0,sizeof(*d));d->registry=r;d->iec=iec;d->rom=rom;d->rom_size=n;d->active_side=1u;d->half_track=36u;d->stepper_phase=0u;d->density_zone=2u;d->sync_level=1u;d->byte_ready_level=1u;d->status=BT_1541_TARGET_OK;bt_via6522_init(&d->via_iec,d,via_iec_read_port,via_iec_write_port,via_iec_control);bt_via6522_init(&d->via_disk,d,via_disk_read_port,via_disk_write_port,via_disk_control);d->via_iec.ca1=(uint8_t)!bt_iec_atn_high(d->iec);b.ctx=d;b.begin_cycle=bus_begin;b.read=bus_read;b.write=bus_write;cpu6510_init(&d->cpu,b);memset(&d->aot,0,sizeof(d->aot));bt_c06_registry_bind_context(r,&d->cpu,&d->aot);if(!bt_aot_set_rendezvous(&d->aot,d,aot_rendezvous)){fail(d,BT_1541_TARGET_AOT_STOP);return false;}return true;}
bool bt_1541_target_attach_side(bt_1541_target*d,unsigned side,const uint8_t*bytes,size_t n,bool wp){bt_media_status s;if(!d||side<1u||side>2u)return false;if(!bt_d64_attach(&d->sides[side-1u],side,bytes,n,wp,&s)){fail(d,BT_1541_TARGET_BAD_MEDIA);return false;}if(side==d->active_side){d->inserted=1u;d->write_protect_level=(uint8_t)wp;load_track(d);}return true;}
bool bt_1541_target_select_side(bt_1541_target*d,unsigned side){if(!d||side<1u||side>2u||!d->sides[side-1u].attached){if(d)fail(d,BT_1541_TARGET_BAD_MEDIA);return false;}d->active_side=(uint8_t)side;d->inserted=1u;d->write_protect_level=d->sides[side-1u].write_protected;load_track(d);return true;}
void bt_1541_target_eject(bt_1541_target*d){if(d){d->inserted=0u;d->gcr_track_bytes=0u;d->write_protect_level=0u;}}
void bt_1541_target_set_semantic_fastloader_mode(bt_1541_target*d,bool enabled){if(d)d->semantic_fastloader_mode=(uint8_t)(enabled?1u:0u);}
bool bt_1541_target_reset(bt_1541_target*d){if(!d||d->failed)return false;bt_via6522_reset(&d->via_iec);bt_via6522_reset(&d->via_disk);d->via_iec.ca1=(uint8_t)!bt_iec_atn_high(d->iec);d->sync_ff_run=0u;d->cpu.cycles=0;d->drive_cycle_target=0;d->c64_cycle_seen=0;d->media_cycle_accumulator=0;d->media_byte_index=0;d->executing_identity_key=0;d->executing_pc=0;d->semantic_fastloader_mode=0u;cpu6510_reset(&d->cpu);refresh_irq(d);load_track(d);return !d->failed;}
bool bt_1541_target_run_static_block(bt_1541_target*d){uint16_t k;int32_t bi;bt_aot_result ar;if(!d||d->failed||!d->registry)return false;refresh_irq(d);/* C06 proves four external JMP sources into the live $0400/$0404 loader. The AOT branch guard validates the target generation before the source JMP executes, so revalidate/commit the live identity at the source boundary, not one instruction too late at the target PC. */if(drive0400_external_source(d->cpu.pc)&&!activate_drive0400_live_identity(d)){fail(d,BT_1541_TARGET_NO_STATIC_BLOCK);return false;}if(d->cpu.nmi_edge_pending||(d->cpu.irq_line&&!d->cpu.irq_sampled)){uint16_t ret=d->cpu.pc;cpu6510_step_result ir;if(!bt_c06_registry_push_hardware_interrupt(d->registry,BT_C06_PROCESSOR_DRIVE_6502,ret)){fail(d,BT_1541_TARGET_REGISTRY_FAILURE);return false;}ir=cpu6510_service_pending_interrupt(&d->cpu);if(!(ir.serviced_irq||ir.serviced_nmi)){fail(d,BT_1541_TARGET_AOT_STOP);return false;}return true;}k=d->registry->drive_owner[d->cpu.pc];if((d->cpu.pc==0x0400u||d->cpu.pc==0x0404u)||(!k&&d->cpu.pc>=0x0400u&&d->cpu.pc<=0x049fu)){/* Every statically proved external entry must revalidate the live $0400 generation. The registry may still name identity 18 from an earlier generation while installer workspace $0407-$0417 has changed; owner presence alone is therefore not proof that the current RAM generation is executable. */if(!activate_drive0400_live_identity(d)){fail(d,BT_1541_TARGET_NO_STATIC_BLOCK);return false;}k=d->registry->drive_owner[d->cpu.pc];}if(!k){fail(d,BT_1541_TARGET_NO_STATIC_BLOCK);return false;}bi=bt_c06_find_block(k,d->cpu.pc);if(bi<0){fail(d,BT_1541_TARGET_NO_STATIC_BLOCK);return false;}d->executing_identity_key=k;d->executing_pc=d->cpu.pc;ar=bt_c06_execute_index(&d->aot,(uint32_t)bi);d->executing_identity_key=0;d->executing_pc=0;if(ar.stop_reason!=BT_AOT_STOP_NONE){fail(d,BT_1541_TARGET_AOT_STOP);return false;}return true;}
bool bt_1541_target_advance_to_c64_cycle(bt_1541_target*d,uint64_t cc){uint64_t target;if(!d||d->failed||cc<d->c64_cycle_seen)return false;d->c64_cycle_seen=cc;target=(cc*(uint64_t)BT_1541_TARGET_CPU_HZ)/985248u;d->drive_cycle_target=target;/* WESTWOOD replaces the BOOT drive loader with a second statically proved upload generation that has no C06 drive identity.  From that handoff onward, BattleTech file transfers are owned by the generated target-specific fastloader semantic service.  Keep VIA/media time monotonic, but never execute an unadmitted drive generation. */if(d->semantic_fastloader_mode){tick_device_to(d,target);d->cpu.cycles=target;return !d->failed;}while(!d->failed){uint32_t need=predict_next_cycles(d);if(!need){/* Preserve the normal fail-closed diagnostic path: a missing owner/block or rejected instruction must never become a silent scheduler stall. */(void)bt_1541_target_run_static_block(d);break;}if(d->cpu.cycles+(uint64_t)need>target)break;if(!bt_1541_target_run_static_block(d))break;}tick_device_to(d,d->cpu.cycles);return !d->failed;}
bool bt_1541_target_failed(const bt_1541_target*d){return !d||d->failed!=0u;}
bt_1541_target_status bt_1541_target_last_status(const bt_1541_target*d){return d?d->status:BT_1541_TARGET_BAD_ARGUMENT;}
