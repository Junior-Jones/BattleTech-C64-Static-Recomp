#ifndef BATTLETECH_C64_C06_AOT_REGISTRY_H
#define BATTLETECH_C64_C06_AOT_REGISTRY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "aot_catalog.h"
#define BT_C06_REGISTRY_BLOCK_COUNT 21637u
#define BT_C06_REGISTRY_IDENTITY_COUNT 20u
#define BT_C06_SHADOW_CAPACITY 1024u
typedef struct bt_c06_shadow_frame { uint16_t return_pc; uint16_t identity_key; uint32_t generation; uint8_t processor_domain; uint8_t c64_view; uint8_t kind; } bt_c06_shadow_frame;
typedef struct bt_c06_registry { uint16_t c64_owner[65536]; uint16_t drive_owner[65536]; uint32_t generation[BT_C06_REGISTRY_IDENTITY_COUNT+1u]; uint8_t blocked[BT_C06_REGISTRY_BLOCK_COUNT]; uint8_t c64_view; uint8_t failed; size_t c64_shadow_depth; size_t drive_shadow_depth; bt_c06_shadow_frame c64_shadow[BT_C06_SHADOW_CAPACITY]; bt_c06_shadow_frame drive_shadow[BT_C06_SHADOW_CAPACITY]; } bt_c06_registry;
void bt_c06_registry_init(bt_c06_registry *r);
bool bt_c06_registry_set_c64_view(bt_c06_registry *r,uint8_t view_bits);
bool bt_c06_registry_begin_identity_load(bt_c06_registry *r,uint32_t identity_key);
bool bt_c06_registry_commit_identity(bt_c06_registry *r,uint32_t identity_key,const char *actual_sha256);
bool bt_c06_registry_apply_smc_write(bt_c06_registry *r,uint32_t source_identity_key,uint16_t write_pc,uint16_t target_address);
bool bt_c06_registry_push_hardware_interrupt(bt_c06_registry *r,uint8_t processor_domain,uint16_t return_pc);
bool bt_c06_registry_block_guard(void *ctx,uint32_t block_index,uint32_t identity_key,uint16_t guest_pc);
bool bt_c06_registry_shadow_push(void *ctx,uint32_t block_index,uint32_t identity_key,uint16_t return_pc,bt_aot_shadow_kind kind);
bool bt_c06_registry_shadow_guard(void *ctx,uint32_t block_index,uint16_t resolved_target,bt_aot_target_class kind);
void bt_c06_registry_bind_context(bt_c06_registry *r,cpu6510 *cpu,bt_aot_context *ctx);
#endif
