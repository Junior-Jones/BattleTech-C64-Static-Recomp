#ifndef BATTLETECH_C64_BT_KERNAL_AOT_GENERATED_H
#define BATTLETECH_C64_BT_KERNAL_AOT_GENERATED_H
#include <stddef.h>
#include <stdint.h>
typedef struct bt_kernal_aot_meta {uint16_t pc;uint8_t opcode;uint8_t kind;uint16_t return_pc;uint16_t succ0;uint16_t succ1;uint8_t succ_count;} bt_kernal_aot_meta;
const bt_kernal_aot_meta *bt_kernal_aot_lookup(uint16_t pc);
size_t bt_kernal_aot_count(void);
extern const uint8_t bt_kernal_cold_vic_regs[0x2f];
extern const uint8_t bt_kernal_default_vectors[0x20];
extern const uint8_t bt_kernal_key_table_unshifted[65];
extern const uint8_t bt_kernal_key_table_shifted[65];
extern const uint8_t bt_kernal_key_table_commodore[65];
extern const uint8_t bt_kernal_key_table_control[65];
#define BT_KERNAL_KEY_TABLE_SIZE 65u
#define BT_KERNAL_KEYCODE_NONE 0x40u
#define BT_KERNAL_KEYBUF_DEFAULT_MAX 0x0Au
#define BT_KERNAL_KEY_REPEAT_DELAY 0x0Au
#define BT_KERNAL_KEY_REPEAT_SPEED 0x04u
#define BT_KERNAL_COLD_VIC_REG_COUNT 0x2fu
#define BT_KERNAL_DEFAULT_VECTOR_COUNT 0x20u
#define BT_KERNAL_PAL_TIMER_A_LATCH 0x4025u
#define BT_KERNAL_PAL_FLAG 0x01u
#define BT_KERNAL_DEFAULT_IRQ_VECTOR 0xEA31u
#define BT_KERNAL_DEFAULT_BRK_VECTOR 0xFE66u
#define BT_KERNAL_DEFAULT_NMI_VECTOR 0xFE47u
#define BT_KERNAL_COLD_SCREEN_BASE 0x0400u
#define BT_KERNAL_COLD_SCREEN_CELLS 1000u
#define BT_KERNAL_COLD_SCREEN_CODE 0x20u
#define BT_KERNAL_COLD_TEXT_COLOR 0x0Eu
#define BT_KERNAL_AOT_ROM_SHA256 "83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721"
#endif
