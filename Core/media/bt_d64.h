#ifndef BATTLETECH_C64_BT_D64_H
#define BATTLETECH_C64_BT_D64_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BT_D64_TRACKS 35u
#define BT_D64_SECTORS 683u
#define BT_D64_SECTOR_SIZE 256u
#define BT_D64_SIZE 174848u
#define BT_D64_DIR_TRACK 18u
#define BT_D64_MAX_FILENAME 16u
typedef enum bt_media_status {BT_MEDIA_OK=0,BT_MEDIA_NO_MEDIA,BT_MEDIA_BAD_SIZE,BT_MEDIA_BAD_HASH,BT_MEDIA_BAD_TRACK_SECTOR,BT_MEDIA_WRITE_PROTECTED,BT_MEDIA_OVERLAY_FULL,BT_MEDIA_DIRECTORY_ERROR,BT_MEDIA_FILE_NOT_FOUND,BT_MEDIA_WRONG_SIDE,BT_MEDIA_UNSUPPORTED_FORMAT} bt_media_status;
typedef struct bt_d64_overlay_entry {uint16_t sector_index;uint8_t bytes[BT_D64_SECTOR_SIZE];uint8_t used;} bt_d64_overlay_entry;
typedef struct bt_d64_image {const uint8_t *base;size_t size;uint8_t side;uint8_t attached;uint8_t write_protected;uint16_t overlay_count;bt_d64_overlay_entry overlay[BT_D64_SECTORS];} bt_d64_image;
typedef struct bt_d64_file {char name[BT_D64_MAX_FILENAME+1u];uint8_t type;uint8_t start_track,start_sector;uint16_t blocks;} bt_d64_file;
const char *bt_d64_expected_sha256(unsigned side);
unsigned bt_d64_sectors_on_track(unsigned track);
bool bt_d64_ts_to_index(unsigned track,unsigned sector,uint16_t *index);
bool bt_d64_attach(bt_d64_image *d,unsigned side,const uint8_t *bytes,size_t size,bool write_protected,bt_media_status *status);
bool bt_d64_read_sector(const bt_d64_image *d,unsigned track,unsigned sector,uint8_t out[BT_D64_SECTOR_SIZE],bt_media_status *status);
bool bt_d64_write_sector(bt_d64_image *d,unsigned track,unsigned sector,const uint8_t in[BT_D64_SECTOR_SIZE],bt_media_status *status);
bool bt_d64_find_file(const bt_d64_image *d,const char *name,bt_d64_file *out,bt_media_status *status);
bool bt_d64_read_file(const bt_d64_image *d,const bt_d64_file *f,uint8_t *out,size_t capacity,size_t *used,bt_media_status *status);
void bt_d64_clear_overlay(bt_d64_image *d);
#ifdef __cplusplus
}
#endif
#endif
