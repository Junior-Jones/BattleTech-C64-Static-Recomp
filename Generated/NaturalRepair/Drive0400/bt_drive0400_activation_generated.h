#ifndef BT_DRIVE0400_ACTIVATION_GENERATED_H
#define BT_DRIVE0400_ACTIVATION_GENERATED_H
#include <stddef.h>
#include <stdint.h>
typedef struct bt_drive0400_guard { uint32_t block_index; uint16_t pc; uint8_t length; const char *instruction_sha256; } bt_drive0400_guard;
extern const bt_drive0400_guard bt_drive0400_guards[];
extern const size_t bt_drive0400_guard_count;
#define BT_DRIVE0400_IDENTITY_KEY 18u
#define BT_DRIVE0400_MUTABLE_LO 0x0407u
#define BT_DRIVE0400_MUTABLE_HI 0x0417u
#define BT_DRIVE0400_BLOCKED_PC 0x0407u
#endif
