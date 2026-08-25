#ifndef BATTLETECH_FRONTEND_INPUT_H
#define BATTLETECH_FRONTEND_INPUT_H

#include "bt_static_core.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum bt_frontend_key {
    BT_FRONTEND_KEY_UP = 1,
    BT_FRONTEND_KEY_DOWN,
    BT_FRONTEND_KEY_LEFT,
    BT_FRONTEND_KEY_RIGHT,
    BT_FRONTEND_KEY_FIRE,
    BT_FRONTEND_KEY_MENU_SPACE,
    BT_FRONTEND_KEY_C64_RETURN,
    BT_FRONTEND_KEY_C64_CURSOR_RIGHT,
    BT_FRONTEND_KEY_C64_CURSOR_DOWN,
    BT_FRONTEND_KEY_C64_Y,
    BT_FRONTEND_KEY_C64_N
} bt_frontend_key;

typedef struct bt_frontend_input_state {
    uint32_t joystick2_mask;
    uint8_t key_down[12];
} bt_frontend_input_state;

void bt_frontend_input_init(bt_frontend_input_state *state);
int bt_frontend_input_reset(bt_static_core *core, bt_frontend_input_state *state);
int bt_frontend_input_submit(bt_static_core *core, bt_frontend_input_state *state,
                             bt_frontend_key key, int pressed);
const char *bt_frontend_key_name(bt_frontend_key key);

#ifdef __cplusplus
}
#endif
#endif
