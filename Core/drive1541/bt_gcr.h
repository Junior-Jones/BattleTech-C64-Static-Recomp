#ifndef BATTLETECH_C64_BT_GCR_H
#define BATTLETECH_C64_BT_GCR_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BT_GCR_MAX_TRACK_BYTES 7692u
#define BT_GCR_SECTOR_STREAM_BYTES 361u
bool bt_gcr_encode4(const uint8_t in[4],uint8_t out[5]);
bool bt_gcr_decode5(const uint8_t in[5],uint8_t out[4]);
unsigned bt_gcr_track_bytes(unsigned track);
bool bt_gcr_build_sector(uint8_t track,uint8_t sector,uint8_t id1,uint8_t id2,const uint8_t data[256],uint8_t out[BT_GCR_SECTOR_STREAM_BYTES]);
bool bt_gcr_build_track(uint8_t track,uint8_t id1,uint8_t id2,const uint8_t *sector_data,size_t sector_data_size,uint8_t *out,size_t capacity,size_t *used);
/* Decode valid sector records from one circular target track. Returns a bit mask of recovered sectors. */
bool bt_gcr_extract_track(uint8_t track,const uint8_t *track_bytes,size_t track_size,uint8_t *sector_data,size_t sector_capacity,uint32_t *sector_mask);
#ifdef __cplusplus
}
#endif
#endif
