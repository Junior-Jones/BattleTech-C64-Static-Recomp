#include "aot_runtime.h"
#include <string.h>
#include <stddef.h>

typedef struct bt_aot_rendezvous_binding { bt_aot_context *ctx; void *rendezvous_ctx; bt_aot_rendezvous_fn fn; } bt_aot_rendezvous_binding;
static bt_aot_rendezvous_binding bt_rendezvous_bindings[16];
bool bt_aot_set_rendezvous(bt_aot_context *ctx,void *rctx,bt_aot_rendezvous_fn fn){size_t i;if(!ctx||!fn)return false;for(i=0;i<16u;i++)if(bt_rendezvous_bindings[i].ctx==ctx||bt_rendezvous_bindings[i].ctx==0){bt_rendezvous_bindings[i].ctx=ctx;bt_rendezvous_bindings[i].rendezvous_ctx=rctx;bt_rendezvous_bindings[i].fn=fn;return true;}return false;}
void bt_aot_clear_rendezvous(bt_aot_context *ctx){size_t i;for(i=0;i<16u;i++)if(bt_rendezvous_bindings[i].ctx==ctx){memset(&bt_rendezvous_bindings[i],0,sizeof(bt_rendezvous_bindings[i]));return;}}
static bool run_rendezvous(bt_aot_context *ctx,uint64_t cyc){size_t i;for(i=0;i<16u;i++)if(bt_rendezvous_bindings[i].ctx==ctx)return bt_rendezvous_bindings[i].fn(bt_rendezvous_bindings[i].rendezvous_ctx,cyc);return true;}

static bt_aot_stop_reason map_cpu_stop(cpu6510_stop_reason s){
    switch(s){
        case CPU6510_STOP_NONE:return BT_AOT_STOP_NONE;
        case CPU6510_STOP_JAM:return BT_AOT_STOP_CPU_JAM;
        case CPU6510_STOP_AOT_PC_MISMATCH:return BT_AOT_STOP_PC_MISMATCH;
        case CPU6510_STOP_AOT_SOURCE_MISMATCH:return BT_AOT_STOP_SOURCE_MISMATCH;
        case CPU6510_STOP_AOT_INTERRUPT_PENDING:return BT_AOT_STOP_INTERRUPT_RENDEZVOUS;
        default:return BT_AOT_STOP_TARGET_REJECTED;
    }
}

bt_aot_result bt_aot_execute_fixed(bt_aot_context *ctx,uint32_t block_index,uint32_t identity_key,uint16_t guest_pc,uint8_t opcode){
    bt_aot_result out; memset(&out,0,sizeof(out));
    out.block_index=block_index; out.identity_key=identity_key; out.guest_pc=guest_pc; out.next_pc=guest_pc; out.opcode=opcode;
    if(!ctx || !ctx->cpu || !ctx->block_guard){ out.stop_reason=BT_AOT_STOP_BLOCK_GUARD_UNAVAILABLE; return out; }
    if(!run_rendezvous(ctx,ctx->cpu->cycles)){ out.stop_reason=BT_AOT_STOP_RENDEZVOUS_REJECTED; return out; }
    if(!ctx->block_guard(ctx->guard_ctx,block_index,identity_key,guest_pc)){ out.stop_reason=BT_AOT_STOP_BLOCK_GUARD_REJECTED; return out; }
    {
        cpu6510_step_result r=cpu6510_step_fixed(ctx->cpu,guest_pc,opcode);
        out.cycles=r.cycles; out.next_pc=ctx->cpu->pc; out.stop_reason=map_cpu_stop(r.stop_reason);
    }
    return out;
}

bt_aot_result bt_aot_note_shadow_push(bt_aot_context *ctx,bt_aot_result r,uint16_t return_pc,bt_aot_shadow_kind kind){
    if(r.stop_reason!=BT_AOT_STOP_NONE)return r;
    if(!ctx || !ctx->shadow_push){r.stop_reason=BT_AOT_STOP_SHADOW_PUSH_UNAVAILABLE;return r;}
    if(!ctx->shadow_push(ctx->guard_ctx,r.block_index,r.identity_key,return_pc,kind)){r.stop_reason=BT_AOT_STOP_SHADOW_PUSH_REJECTED;return r;}
    return r;
}

bt_aot_result bt_aot_finish_target(bt_aot_context *ctx,bt_aot_result r,bt_aot_target_class target_class){
    if(r.stop_reason!=BT_AOT_STOP_NONE)return r;
    r.target_class=target_class;
    switch(target_class){
        case BT_AOT_TARGET_GENERATED:
        case BT_AOT_TARGET_SYSTEM_ROM_EXIT:
        case BT_AOT_TARGET_DEVICE_EXIT:
            return r;
        case BT_AOT_TARGET_SHADOW_RETURN:
            if(!ctx || !ctx->shadow_guard){r.stop_reason=BT_AOT_STOP_SHADOW_GUARD_UNAVAILABLE;return r;}
            if(!ctx->shadow_guard(ctx->guard_ctx,r.block_index,r.next_pc,target_class)){r.stop_reason=BT_AOT_STOP_SHADOW_GUARD_REJECTED;return r;}
            return r;
        case BT_AOT_TARGET_INTERRUPT_VECTOR:
            return bt_aot_note_shadow_push(ctx,r,(uint16_t)(r.guest_pc+2u),BT_AOT_SHADOW_INTERRUPT);
        case BT_AOT_TARGET_STOP:
            return r;
        case BT_AOT_TARGET_TRAP:
        case BT_AOT_TARGET_INVALID:
        default:
            r.stop_reason=BT_AOT_STOP_TARGET_REJECTED;return r;
    }
}
