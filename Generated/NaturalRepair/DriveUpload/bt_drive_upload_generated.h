#ifndef BATTLETECH_C64_BT_DRIVE_UPLOAD_GENERATED_H
#define BATTLETECH_C64_BT_DRIVE_UPLOAD_GENERATED_H
#include <stdint.h>
#define BT_DRIVE_UPLOAD_RECORD_COUNT 4u
typedef struct bt_drive_upload_record { uint16_t entry,source,target,bytes; uint8_t blocks,owner_identity_key,commit_identity_key,replace_identity_key; const char *owner_sha256; const char *sha256; } bt_drive_upload_record;
extern const bt_drive_upload_record bt_drive_upload_records[BT_DRIVE_UPLOAD_RECORD_COUNT];
#endif
