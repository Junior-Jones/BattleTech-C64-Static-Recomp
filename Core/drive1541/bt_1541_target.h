#ifndef BATTLETECH_C64_BT_1541_TARGET_H
#define BATTLETECH_C64_BT_1541_TARGET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../cpu6510/cpu6510.h"
#include "../aot/aot_runtime.h"
#include "bt_iec.h"
#include "bt_via6522.h"
#include "bt_gcr.h"
#include "../media/bt_d64.h"
#include "../../Generated/C06/Source/aot_registry.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BT_1541_TARGET_CPU_HZ 1000000u
#define BT_1541_TARGET_RAM_BYTES 2048u
#define BT_1541_TARGET_ROM_BYTES 16384u
#define BT_1541_TARGET_DEVICE 8u

typedef enum bt_1541_target_status {
 BT_1541_TARGET_OK=0,
 BT_1541_TARGET_BAD_ARGUMENT,
 BT_1541_TARGET_BAD_DRIVE_ROM,
 BT_1541_TARGET_BAD_MEDIA,
 BT_1541_TARGET_NO_STATIC_BLOCK,
 BT_1541_TARGET_AOT_STOP,
 BT_1541_TARGET_UNSUPPORTED_DEVICE_STATE,
 BT_1541_TARGET_REGISTRY_FAILURE
} bt_1541_target_status;

typedef struct bt_1541_target {
 cpu6510 cpu;
 bt_aot_context aot;
 bt_c06_registry *registry;
 bt_iec_bus *iec;
 bt_via6522 via_iec;
 bt_via6522 via_disk;
 uint8_t ram[BT_1541_TARGET_RAM_BYTES];
 const uint8_t *rom;
 size_t rom_size;
 bt_d64_image sides[2];
 uint8_t active_side;
 uint8_t inserted;
 uint8_t motor_on;
 uint8_t activity_led;
 uint8_t density_zone;
 uint8_t stepper_phase;
 uint8_t half_track;
 uint8_t disk_byte;
 uint8_t disk_write_byte;
 uint8_t disk_output_enable;
 uint8_t disk_set_overflow_enable;
 uint8_t sync_level;
 uint8_t sync_ff_run;
 uint8_t byte_ready_level;
 uint8_t write_protect_level;
 uint8_t failed;
 uint8_t semantic_fastloader_mode;
 uint32_t executing_identity_key;
 uint16_t executing_pc;
 uint64_t c64_cycle_seen;
 uint64_t drive_cycle_target;
 uint64_t media_cycle_accumulator;
 uint64_t media_byte_index;
 uint8_t gcr_track[BT_GCR_MAX_TRACK_BYTES];
 size_t gcr_track_bytes;
 bt_1541_target_status status;
} bt_1541_target;

bool bt_1541_target_init(bt_1541_target *d,bt_c06_registry *registry,bt_iec_bus *iec,const uint8_t *drive_rom,size_t drive_rom_size);
bool bt_1541_target_attach_side(bt_1541_target *d,unsigned side,const uint8_t *bytes,size_t size,bool write_protected);
bool bt_1541_target_select_side(bt_1541_target *d,unsigned side);
void bt_1541_target_eject(bt_1541_target *d);
bool bt_1541_target_reset(bt_1541_target *d);
bool bt_1541_target_advance_to_c64_cycle(bt_1541_target *d,uint64_t c64_cycle);
bool bt_1541_target_run_static_block(bt_1541_target *d);
void bt_1541_target_set_semantic_fastloader_mode(bt_1541_target *d,bool enabled);
bool bt_1541_target_failed(const bt_1541_target *d);
bt_1541_target_status bt_1541_target_last_status(const bt_1541_target *d);
#ifdef __cplusplus
}
#endif
#endif
