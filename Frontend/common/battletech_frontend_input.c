#include "battletech_frontend_input.h"
#include <string.h>

static int submit_event(bt_static_core *core, bt_static_input_kind kind,
                        uint32_t control, uint64_t value)
{
    bt_static_status status;
    bt_static_input_event event;
    if (!core) return BT_STATIC_ERR_BAD_ARGUMENT;
    memset(&status, 0, sizeof(status));
    if (bt_static_core_get_status(core, &status) != BT_STATIC_OK)
        return BT_STATIC_ERR_INTERNAL;
    memset(&event, 0, sizeof(event));
    event.cycle = status.cycle;
    event.kind = kind;
    event.control = control;
    event.value = value;
    return bt_static_core_submit_input(core, &event);
}

void bt_frontend_input_init(bt_frontend_input_state *state)
{
    if (state) memset(state, 0, sizeof(*state));
}

int bt_frontend_input_reset(bt_static_core *core, bt_frontend_input_state *state)
{
    static const uint32_t matrix_controls[] = {
        BT_STATIC_KEY_SPACE,
        BT_STATIC_KEY_RETURN,
        BT_STATIC_KEY_CURSOR_RIGHT,
        BT_STATIC_KEY_CURSOR_DOWN,
        BT_STATIC_KEY_Y,
        BT_STATIC_KEY_N
    };
    size_t i;
    if (!core || !state) return BT_STATIC_ERR_BAD_ARGUMENT;
    if (submit_event(core, BT_STATIC_INPUT_JOYSTICK2, 0u, 0u) != BT_STATIC_OK)
        return BT_STATIC_ERR_INTERNAL;
    for (i = 0; i < sizeof(matrix_controls) / sizeof(matrix_controls[0]); ++i) {
        if (submit_event(core, BT_STATIC_INPUT_KEY_MATRIX, matrix_controls[i], 0u) != BT_STATIC_OK)
            return BT_STATIC_ERR_INTERNAL;
    }
    memset(state, 0, sizeof(*state));
    return BT_STATIC_OK;
}

static uint32_t joystick_bit(bt_frontend_key key)
{
    switch (key) {
    case BT_FRONTEND_KEY_UP: return BT_STATIC_JOYSTICK_UP;
    case BT_FRONTEND_KEY_DOWN: return BT_STATIC_JOYSTICK_DOWN;
    case BT_FRONTEND_KEY_LEFT: return BT_STATIC_JOYSTICK_LEFT;
    case BT_FRONTEND_KEY_RIGHT: return BT_STATIC_JOYSTICK_RIGHT;
    case BT_FRONTEND_KEY_FIRE: return BT_STATIC_JOYSTICK_FIRE;
    default: return 0u;
    }
}

static uint32_t matrix_control(bt_frontend_key key)
{
    switch (key) {
    case BT_FRONTEND_KEY_MENU_SPACE: return BT_STATIC_KEY_SPACE;
    case BT_FRONTEND_KEY_C64_RETURN: return BT_STATIC_KEY_RETURN;
    case BT_FRONTEND_KEY_C64_CURSOR_RIGHT: return BT_STATIC_KEY_CURSOR_RIGHT;
    case BT_FRONTEND_KEY_C64_CURSOR_DOWN: return BT_STATIC_KEY_CURSOR_DOWN;
    case BT_FRONTEND_KEY_C64_Y: return BT_STATIC_KEY_Y;
    case BT_FRONTEND_KEY_C64_N: return BT_STATIC_KEY_N;
    default: return 0xffffffffu;
    }
}

int bt_frontend_input_submit(bt_static_core *core, bt_frontend_input_state *state,
                             bt_frontend_key key, int pressed)
{
    uint32_t bit, control;
    unsigned index;
    if (!core || !state || key < BT_FRONTEND_KEY_UP || key > BT_FRONTEND_KEY_C64_N)
        return BT_STATIC_ERR_BAD_ARGUMENT;
    index = (unsigned)key;
    pressed = pressed ? 1 : 0;
    if (state->key_down[index] == (uint8_t)pressed) return BT_STATIC_OK;

    bit = joystick_bit(key);
    if (bit) {
        uint32_t next = state->joystick2_mask;
        if (pressed) next |= bit; else next &= ~bit;
        if (submit_event(core, BT_STATIC_INPUT_JOYSTICK2, 0u, next) != BT_STATIC_OK)
            return BT_STATIC_ERR_INTERNAL;
        state->joystick2_mask = next;
        state->key_down[index] = (uint8_t)pressed;
        return BT_STATIC_OK;
    }

    control = matrix_control(key);
    if (control == 0xffffffffu) return BT_STATIC_ERR_BAD_ARGUMENT;
    if (submit_event(core, BT_STATIC_INPUT_KEY_MATRIX, control, (uint64_t)pressed) != BT_STATIC_OK)
        return BT_STATIC_ERR_INTERNAL;
    state->key_down[index] = (uint8_t)pressed;
    return BT_STATIC_OK;
}

const char *bt_frontend_key_name(bt_frontend_key key)
{
    switch (key) {
    case BT_FRONTEND_KEY_UP: return "joystick2-up";
    case BT_FRONTEND_KEY_DOWN: return "joystick2-down";
    case BT_FRONTEND_KEY_LEFT: return "joystick2-left";
    case BT_FRONTEND_KEY_RIGHT: return "joystick2-right";
    case BT_FRONTEND_KEY_FIRE: return "joystick2-fire";
    case BT_FRONTEND_KEY_MENU_SPACE: return "c64-space";
    case BT_FRONTEND_KEY_C64_RETURN: return "c64-return";
    case BT_FRONTEND_KEY_C64_CURSOR_RIGHT: return "c64-cursor-right";
    case BT_FRONTEND_KEY_C64_CURSOR_DOWN: return "c64-cursor-down";
    case BT_FRONTEND_KEY_C64_Y: return "c64-y";
    case BT_FRONTEND_KEY_C64_N: return "c64-n";
    default: return "unknown";
    }
}
