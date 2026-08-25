#ifndef BT_FASTLOADER_RECORDS_H
#define BT_FASTLOADER_RECORDS_H
#include <stddef.h>
#include <stdint.h>
typedef struct bt_fastloader_record { uint8_t owner_identity_key,side,control_side,attr; uint16_t entry,dest; const char *name; uint32_t bytes; const char *sha256; } bt_fastloader_record;
extern const bt_fastloader_record bt_fastloader_records[];
extern const size_t bt_fastloader_record_count;
#endif
