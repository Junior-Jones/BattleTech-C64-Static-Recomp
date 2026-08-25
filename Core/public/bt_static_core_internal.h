#ifndef BATTLETECH_C64_BT_STATIC_CORE_INTERNAL_H
#define BATTLETECH_C64_BT_STATIC_CORE_INTERNAL_H
#include "bt_static_core.h"
#include "../machine/c64_machine.h"
#include "../drive1541/bt_1541_target.h"
#include "../drive1541/bt_iec.h"
#include "../media/bt_d64.h"
#include "../kernal/bt_kernal_target.h"
#include "../../Generated/C06/Source/aot_registry.h"
#include "../../Generated/C06/Source/aot_catalog.h"
#include <stdint.h>
#define BT_STATIC_BASIC_BYTES 8192u
#define BT_STATIC_KERNAL_BYTES 8192u
#define BT_STATIC_CHARGEN_BYTES 4096u
#define BT_STATIC_DRIVE_ROM_BYTES 16384u
#define BT_STATIC_AUDIO_QUEUE_SAMPLES 65536u
#define BT_STATIC_DIAG_MESSAGE_BYTES 192u
#define BT_STATIC_TRACE_DISABLED 0u

typedef struct bt_static_diag_store {bt_static_diagnostic view;char message[BT_STATIC_DIAG_MESSAGE_BYTES];} bt_static_diag_store;
struct bt_static_core {
 bt_static_allocator allocator;
 bt_static_trace_hook trace;
 uint8_t basic[BT_STATIC_BASIC_BYTES],kernal[BT_STATIC_KERNAL_BYTES],chargen[BT_STATIC_CHARGEN_BYTES],drive_rom[BT_STATIC_DRIVE_ROM_BYTES];
 uint8_t media_bytes[BT_STATIC_CORE_D64_SIDE_COUNT][BT_D64_SIZE];
 uint8_t rom_hash[4][32],media_hash[BT_STATIC_CORE_D64_SIDE_COUNT][32];
 uint8_t system_loaded,media_loaded,reset_done;
 uint64_t deterministic_seed;
 bt_c06_registry registry;
 bt_kernal_target_state kernal_target;
 bt_iec_bus iec;
 bt_1541_target drive;
 bt_c64_machine machine;
 bt_aot_context c64_aot;
 uint32_t executing_identity_key;
 uint16_t executing_pc;
 bt_static_stop_reason last_stop;
 bt_static_diag_store diag;
 int16_t audio_queue[BT_STATIC_AUDIO_QUEUE_SAMPLES];
 size_t audio_read,audio_write,audio_count;
 uint64_t audio_first_cycle;
};
void *bt_static_alloc(const bt_static_core *core,size_t size);
void bt_static_free(const bt_static_core *core,void *ptr);
void bt_static_set_diag(bt_static_core *core,bt_static_error code,bt_static_stop_reason stop,const char *message);
void bt_static_emit_trace(const bt_static_core *core,bt_static_trace_kind kind,uint32_t identity,uint16_t pc,uint32_t code,uint64_t value);
int bt_static_prepare_runtime(bt_static_core *core);
int bt_static_snapshot_save_internal(const bt_static_core *core,void *destination,size_t size);
int bt_static_snapshot_load_internal(bt_static_core *core,const void *source,size_t size);
size_t bt_static_snapshot_size_internal(const bt_static_core *core);
int bt_static_snapshot_state_hash_internal(const bt_static_core *core,uint8_t out[32]);
#endif
