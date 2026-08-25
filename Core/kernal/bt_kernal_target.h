#ifndef BATTLETECH_C64_BT_KERNAL_TARGET_H
#define BATTLETECH_C64_BT_KERNAL_TARGET_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct bt_kernal_target_state {
    uint8_t lfn;
    uint8_t device;
    uint8_t secondary;
    uint8_t name_len;
    uint8_t name[16];
    uint8_t current_input;
    uint8_t current_output;
    uint8_t status_90;
    uint8_t bootstrapped;
    uint32_t service_count;
    uint8_t aot_call_depth;
    uint16_t aot_call_return[32];
} bt_kernal_target_state;

struct bt_static_core;
void bt_kernal_target_state_init(bt_kernal_target_state *k);
bool bt_kernal_target_cold_boot(struct bt_static_core *core);
/* Returns true when the exact target KERNAL entry was handled and execution
   returned to a statically admitted C64 identity. Unsupported entries return false. */
bool bt_kernal_target_service(struct bt_static_core *core,uint16_t entry);
/* BattleTech's sealed BOOT fastloader P_IE is a target service, not a generic IEC emulator. */
bool bt_battletech_fastloader_service(struct bt_static_core *core,uint16_t entry);
/* BattleTech BOOT P_IG: bounded semantic projection of its two statically sealed M-W uploads. */
bool bt_battletech_drive_upload_service(struct bt_static_core *core,uint16_t entry);
/* Admit only BattleTech BOOT's self-modified IRQ-chain trampoline when it
   still targets the pinned KERNAL default IRQ vector. */
bool bt_kernal_target_dynamic_trampoline(struct bt_static_core *core,uint16_t from_pc,uint16_t target);

#ifdef __cplusplus
}
#endif
#endif
