#ifndef BATTLETECH_C64_C06_AOT_CATALOG_H
#define BATTLETECH_C64_C06_AOT_CATALOG_H
#include <stddef.h>
#include <stdint.h>
#include "aot_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum bt_c06_processor_domain { BT_C06_PROCESSOR_C64_6510=1, BT_C06_PROCESSOR_DRIVE_6502=2 } bt_c06_processor_domain;
typedef struct bt_c06_block_meta { uint32_t identity_key; uint16_t pc; uint8_t opcode; uint8_t length; uint32_t successor_offset; uint16_t successor_count; uint16_t bus_flags; uint8_t processor_domain; uint8_t c64_view_mask; bt_aot_target_class special_class; } bt_c06_block_meta;
typedef struct bt_c06_identity_meta { uint32_t identity_key; const char *identity_id; const char *processor; uint8_t processor_domain; uint16_t range_start; uint16_t range_end; const char *epoch_id; const char *byte_hash; } bt_c06_identity_meta;
size_t bt_c06_block_count(void);
size_t bt_c06_identity_count(void);
const bt_c06_block_meta *bt_c06_block_meta_at(uint32_t index);
const bt_c06_identity_meta *bt_c06_identity_meta_at(uint32_t identity_key);
bt_aot_target_class bt_c06_classify_target(uint32_t block_index,uint16_t target);
bt_aot_result bt_c06_execute_index(bt_aot_context *ctx,uint32_t block_index);
int32_t bt_c06_find_block(uint32_t identity_key,uint16_t guest_pc);
#ifdef __cplusplus
}
#endif
#endif
