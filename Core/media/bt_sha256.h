#ifndef BATTLETECH_C64_BT_SHA256_H
#define BATTLETECH_C64_BT_SHA256_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bt_sha256_ctx { uint32_t h[8]; uint64_t total; uint8_t block[64]; size_t used; } bt_sha256_ctx;
void bt_sha256_init(bt_sha256_ctx *c);
void bt_sha256_update(bt_sha256_ctx *c,const void *data,size_t size);
void bt_sha256_final(bt_sha256_ctx *c,uint8_t out[32]);
void bt_sha256(const void *data,size_t size,uint8_t out[32]);
bool bt_sha256_equal_hex(const uint8_t digest[32],const char *hex64);
#ifdef __cplusplus
}
#endif
#endif
