#ifndef BATTLETECH_C64_INPUT_H
#define BATTLETECH_C64_INPUT_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {C64_JOY_UP=0x01,C64_JOY_DOWN=0x02,C64_JOY_LEFT=0x04,C64_JOY_RIGHT=0x08,C64_JOY_FIRE=0x10};
typedef struct c64_input {uint64_t keyboard;uint8_t joystick1;uint8_t joystick2;bool restore;} c64_input;
void c64_input_init(c64_input *in);
bool c64_input_set_key(c64_input *in,uint8_t row,uint8_t column,bool pressed);
void c64_input_set_joystick(c64_input *in,unsigned port,uint8_t active_low_controls);
void c64_input_set_restore(c64_input *in,bool pressed);
uint8_t c64_input_cia1_pins(const c64_input *in,unsigned port,uint8_t pra,uint8_t ddra,uint8_t prb,uint8_t ddrb);
#ifdef __cplusplus
}
#endif
#endif
