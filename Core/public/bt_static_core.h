#ifndef BATTLETECH_C64_BT_STATIC_CORE_H
#define BATTLETECH_C64_BT_STATIC_CORE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BT_STATIC_CORE_API_VERSION_MAJOR 1u
#define BT_STATIC_CORE_API_VERSION_MINOR 0u
#define BT_STATIC_CORE_API_VERSION ((BT_STATIC_CORE_API_VERSION_MAJOR<<16)|BT_STATIC_CORE_API_VERSION_MINOR)
#define BT_STATIC_CORE_SNAPSHOT_VERSION 2u
#define BT_STATIC_CORE_D64_SIDE_COUNT 2u
#define BT_STATIC_CORE_FRAME_WIDTH 384u
#define BT_STATIC_CORE_FRAME_HEIGHT 272u
#define BT_STATIC_CORE_PAL_CYCLES_PER_FRAME 19656u
#define BT_STATIC_CORE_AUDIO_SAMPLE_RATE 44100u
#define BT_STATIC_CORE_DIAGNOSTIC_TEXT 192u

typedef struct bt_static_core bt_static_core;

typedef void *(*bt_static_alloc_fn)(void *ctx,size_t size);
typedef void (*bt_static_free_fn)(void *ctx,void *ptr);
typedef struct bt_static_allocator {void *ctx;bt_static_alloc_fn alloc;bt_static_free_fn free;} bt_static_allocator;

typedef struct bt_static_blob {const uint8_t *data;size_t size;uint8_t sha256[32];} bt_static_blob;
typedef struct bt_static_system_rom_set {bt_static_blob basic,kernal,chargen,drive1541ii;} bt_static_system_rom_set;
typedef struct bt_static_media_pair {bt_static_blob side[BT_STATIC_CORE_D64_SIDE_COUNT];} bt_static_media_pair;

typedef enum bt_static_error {
 BT_STATIC_OK=0,
 BT_STATIC_ERR_BAD_ARGUMENT,
 BT_STATIC_ERR_API_VERSION,
 BT_STATIC_ERR_WRONG_PROFILE,
 BT_STATIC_ERR_WRONG_SYSTEM_ROM,
 BT_STATIC_ERR_WRONG_MEDIA,
 BT_STATIC_ERR_MEDIA_NOT_READY,
 BT_STATIC_ERR_UNSUPPORTED_TARGET_HARDWARE,
 BT_STATIC_ERR_UNKNOWN_TARGET,
 BT_STATIC_ERR_UNKNOWN_EPOCH,
 BT_STATIC_ERR_CPU_JAM,
 BT_STATIC_ERR_SYSTEM_ROM_EXIT,
 BT_STATIC_ERR_SNAPSHOT_MAGIC,
 BT_STATIC_ERR_SNAPSHOT_VERSION,
 BT_STATIC_ERR_SNAPSHOT_IDENTITY,
 BT_STATIC_ERR_SNAPSHOT_TRUNCATED,
 BT_STATIC_ERR_SNAPSHOT_CORRUPT,
 BT_STATIC_ERR_SNAPSHOT_STATE,
 BT_STATIC_ERR_STEP_LIMIT,
 BT_STATIC_ERR_INTERNAL
} bt_static_error;

typedef enum bt_static_stop_reason {
 BT_STATIC_STOP_NONE=0,
 BT_STATIC_STOP_FRAME_READY,
 BT_STATIC_STOP_TARGET_REACHED,
 BT_STATIC_STOP_SYSTEM_ROM_EXIT,
 BT_STATIC_STOP_CPU_JAM,
 BT_STATIC_STOP_UNKNOWN_TARGET,
 BT_STATIC_STOP_UNKNOWN_EPOCH,
 BT_STATIC_STOP_UNSUPPORTED_TARGET_HARDWARE,
 BT_STATIC_STOP_MEDIA_ERROR,
 BT_STATIC_STOP_STEP_LIMIT,
 BT_STATIC_STOP_FATAL
} bt_static_stop_reason;

typedef enum bt_static_input_kind {BT_STATIC_INPUT_JOYSTICK2=1,BT_STATIC_INPUT_KEY_MATRIX=2,BT_STATIC_INPUT_RESTORE=3} bt_static_input_kind;
enum {BT_STATIC_JOYSTICK_UP=0x01,BT_STATIC_JOYSTICK_DOWN=0x02,BT_STATIC_JOYSTICK_LEFT=0x04,BT_STATIC_JOYSTICK_RIGHT=0x08,BT_STATIC_JOYSTICK_FIRE=0x10};
#define BT_STATIC_KEY_MATRIX_CONTROL(row,column) ((((uint32_t)(row)&7u)<<8)|((uint32_t)(column)&7u))
#define BT_STATIC_KEY_RETURN BT_STATIC_KEY_MATRIX_CONTROL(1u,0u)
#define BT_STATIC_KEY_CURSOR_RIGHT BT_STATIC_KEY_MATRIX_CONTROL(2u,0u)
#define BT_STATIC_KEY_CURSOR_DOWN BT_STATIC_KEY_MATRIX_CONTROL(7u,0u)
#define BT_STATIC_KEY_LEFT_SHIFT BT_STATIC_KEY_MATRIX_CONTROL(7u,1u)
#define BT_STATIC_KEY_RIGHT_SHIFT BT_STATIC_KEY_MATRIX_CONTROL(4u,6u)
#define BT_STATIC_KEY_SPACE BT_STATIC_KEY_MATRIX_CONTROL(4u,7u)
#define BT_STATIC_KEY_Y BT_STATIC_KEY_MATRIX_CONTROL(1u,3u)
#define BT_STATIC_KEY_N BT_STATIC_KEY_MATRIX_CONTROL(7u,4u)
typedef struct bt_static_input_event {uint64_t cycle;bt_static_input_kind kind;uint32_t control;int32_t value;} bt_static_input_event;

typedef struct bt_static_run_result {
 bt_static_stop_reason reason;uint64_t start_cycle,end_cycle,completed_frame;uint16_t c64_pc,drive_pc;uint8_t active_disk_side;uint32_t diagnostic_code;uint64_t native_blocks;
} bt_static_run_result;

typedef enum bt_static_trace_kind {
 BT_STATIC_TRACE_C64_BLOCK=1,BT_STATIC_TRACE_DRIVE_BLOCK=2,BT_STATIC_TRACE_INPUT=3,BT_STATIC_TRACE_MEDIA=4,BT_STATIC_TRACE_SNAPSHOT=5,BT_STATIC_TRACE_STOP=6
} bt_static_trace_kind;
typedef struct bt_static_trace_event {bt_static_trace_kind kind;uint64_t cycle;uint32_t identity_key;uint16_t pc;uint32_t code;uint64_t value;} bt_static_trace_event;
typedef void (*bt_static_trace_fn)(void *ctx,const bt_static_trace_event *event);
typedef struct bt_static_trace_hook {void *ctx;bt_static_trace_fn fn;} bt_static_trace_hook;

typedef struct bt_static_create_info {
 size_t struct_size;uint32_t requested_api_version;bt_static_allocator allocator;bt_static_trace_hook trace;
} bt_static_create_info;

typedef struct bt_static_video_frame {const uint8_t *pixels;uint32_t width,height,pitch,pixel_format;uint64_t completed_cycle,frame_number;} bt_static_video_frame;
typedef struct bt_static_status {uint64_t cycle,completed_frame;uint16_t c64_pc,drive_pc;uint8_t active_disk_side,media_inserted;bt_static_stop_reason stop_reason;bt_static_error error;} bt_static_status;

typedef struct bt_static_diagnostic {bt_static_error code;bt_static_stop_reason stop_reason;uint64_t cycle;uint16_t c64_pc,drive_pc;const char *message;} bt_static_diagnostic;

uint32_t bt_static_core_api_version(void);
uint32_t bt_static_core_snapshot_version(void);
const char *bt_static_core_aot_authority_sha256(void);

bt_static_core *bt_static_core_create(const bt_static_create_info *info);
void bt_static_core_destroy(bt_static_core *core);
int bt_static_core_load_system_roms(bt_static_core *core,const bt_static_system_rom_set *roms);
int bt_static_core_load_media_pair(bt_static_core *core,const bt_static_media_pair *media);
int bt_static_core_reset(bt_static_core *core,uint64_t deterministic_seed);

int bt_static_core_select_disk_side(bt_static_core *core,unsigned side);
int bt_static_core_eject_disk(bt_static_core *core);
unsigned bt_static_core_active_disk_side(const bt_static_core *core);

int bt_static_core_submit_input(bt_static_core *core,const bt_static_input_event *event);
int bt_static_core_run_until_cycle(bt_static_core *core,uint64_t target_cycle,uint64_t native_block_limit,bt_static_run_result *result);
int bt_static_core_advance_frame(bt_static_core *core,uint64_t native_block_limit,bt_static_run_result *result);

int bt_static_core_get_video(const bt_static_core *core,bt_static_video_frame *frame);
size_t bt_static_core_audio_available(const bt_static_core *core);
size_t bt_static_core_audio_read(bt_static_core *core,int16_t *output,size_t sample_capacity);
void bt_static_core_audio_clear(bt_static_core *core);

size_t bt_static_core_snapshot_size(const bt_static_core *core);
int bt_static_core_snapshot_save(const bt_static_core *core,void *destination,size_t size);
int bt_static_core_snapshot_load(bt_static_core *core,const void *source,size_t size);
int bt_static_core_state_sha256(const bt_static_core *core,uint8_t output[32]);

/* Persistent writable-media overlay.  This is intentionally separate from a
   snapshot: it preserves only sector writes made to the two verified D64
   images, so BattleTech's original G0..G5 save slots survive an app restart
   without restoring CPU/video/runtime state.  The blob is self-identifying,
   checksummed and bound to the exact two base-media SHA-256 identities. */
size_t bt_static_core_persistent_media_size(const bt_static_core *core);
int bt_static_core_persistent_media_export(const bt_static_core *core,void *destination,size_t size);
int bt_static_core_persistent_media_import(bt_static_core *core,const void *source,size_t size);

int bt_static_core_set_trace_hook(bt_static_core *core,bt_static_trace_hook hook);
int bt_static_core_get_status(const bt_static_core *core,bt_static_status *status);
const bt_static_diagnostic *bt_static_core_last_diagnostic(const bt_static_core *core);
const char *bt_static_error_name(bt_static_error error);

#ifdef __cplusplus
}
#endif
#endif
