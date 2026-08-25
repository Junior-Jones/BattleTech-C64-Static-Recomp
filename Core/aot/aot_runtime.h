#ifndef BATTLETECH_C64_AOT_RUNTIME_H
#define BATTLETECH_C64_AOT_RUNTIME_H
#include <stdbool.h>
#include <stdint.h>
#include "cpu6510.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum bt_aot_stop_reason {
    BT_AOT_STOP_NONE=0,
    BT_AOT_STOP_BLOCK_GUARD_UNAVAILABLE,
    BT_AOT_STOP_BLOCK_GUARD_REJECTED,
    BT_AOT_STOP_RENDEZVOUS_REJECTED,
    BT_AOT_STOP_CPU_JAM,
    BT_AOT_STOP_PC_MISMATCH,
    BT_AOT_STOP_SOURCE_MISMATCH,
    BT_AOT_STOP_INTERRUPT_RENDEZVOUS,
    BT_AOT_STOP_TARGET_REJECTED,
    BT_AOT_STOP_SHADOW_PUSH_UNAVAILABLE,
    BT_AOT_STOP_SHADOW_PUSH_REJECTED,
    BT_AOT_STOP_SHADOW_GUARD_UNAVAILABLE,
    BT_AOT_STOP_SHADOW_GUARD_REJECTED
} bt_aot_stop_reason;

typedef enum bt_aot_target_class {
    BT_AOT_TARGET_INVALID=0,
    BT_AOT_TARGET_GENERATED=1,
    BT_AOT_TARGET_SYSTEM_ROM_EXIT=2,
    BT_AOT_TARGET_DEVICE_EXIT=3,
    BT_AOT_TARGET_SHADOW_RETURN=4,
    BT_AOT_TARGET_INTERRUPT_VECTOR=5,
    BT_AOT_TARGET_TRAP=6,
    BT_AOT_TARGET_STOP=7
} bt_aot_target_class;

typedef enum bt_aot_shadow_kind {
    BT_AOT_SHADOW_CALL=1,
    BT_AOT_SHADOW_INTERRUPT=2
} bt_aot_shadow_kind;

typedef bool (*bt_aot_rendezvous_fn)(void *ctx,uint64_t cpu_cycle);
typedef bool (*bt_aot_block_guard_fn)(void *ctx,uint32_t block_index,uint32_t identity_key,uint16_t guest_pc);
typedef bool (*bt_aot_shadow_push_fn)(void *ctx,uint32_t block_index,uint32_t identity_key,uint16_t return_pc,bt_aot_shadow_kind kind);
typedef bool (*bt_aot_shadow_guard_fn)(void *ctx,uint32_t block_index,uint16_t resolved_target,bt_aot_target_class kind);

typedef struct bt_aot_context {
    cpu6510 *cpu;
    void *guard_ctx;
    bt_aot_block_guard_fn block_guard;
    bt_aot_shadow_push_fn shadow_push;
    bt_aot_shadow_guard_fn shadow_guard;
} bt_aot_context;

typedef struct bt_aot_result {
    uint32_t block_index;
    uint32_t identity_key;
    uint16_t guest_pc;
    uint16_t next_pc;
    uint32_t cycles;
    uint8_t opcode;
    bt_aot_target_class target_class;
    bt_aot_stop_reason stop_reason;
} bt_aot_result;

bool bt_aot_set_rendezvous(bt_aot_context *ctx,void *rendezvous_ctx,bt_aot_rendezvous_fn rendezvous);
void bt_aot_clear_rendezvous(bt_aot_context *ctx);
bt_aot_result bt_aot_execute_fixed(bt_aot_context *ctx,uint32_t block_index,uint32_t identity_key,uint16_t guest_pc,uint8_t opcode);
bt_aot_result bt_aot_note_shadow_push(bt_aot_context *ctx,bt_aot_result r,uint16_t return_pc,bt_aot_shadow_kind kind);
bt_aot_result bt_aot_finish_target(bt_aot_context *ctx,bt_aot_result r,bt_aot_target_class target_class);

#ifdef __cplusplus
}
#endif
#endif
