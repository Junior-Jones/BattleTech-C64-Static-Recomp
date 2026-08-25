#ifndef BATTLETECH_C64_CIA6526_H
#define BATTLETECH_C64_CIA6526_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {CIA6526_ICR_TA=0x01,CIA6526_ICR_TB=0x02,CIA6526_ICR_TOD=0x04,CIA6526_ICR_SDR=0x08,CIA6526_ICR_FLAG=0x10};
/* Return resolved physical pin levels. External active-low devices may pull an output-high pin low. */
typedef uint8_t (*cia6526_pin_read_fn)(void *ctx,unsigned port,uint8_t pra,uint8_t ddra,uint8_t prb,uint8_t ddrb);
typedef void (*cia6526_port_changed_fn)(void *ctx,unsigned port,uint8_t latch,uint8_t ddr,uint8_t driven);
typedef struct cia6526_timer {uint16_t counter,latch,state;uint8_t output_toggle,pulse;} cia6526_timer;
typedef struct cia6526 {
 uint8_t pra,prb,ddra,ddrb,sdr,cra,crb;
 cia6526_timer ta,tb;
 uint8_t tod[4],alarm[4],tod_latch[4],tod_latched,tod_stopped,tod_pulses;
 uint8_t icr_flags,icr_mask,irq_line,irq_delay;
 uint8_t cnt_level,sp_level,flag_level,serial_shift,serial_bits;
 uint64_t cycle;
 void *io_ctx;cia6526_pin_read_fn pin_read;cia6526_port_changed_fn port_changed;
} cia6526;
void cia6526_init(cia6526 *c,void *io_ctx,cia6526_pin_read_fn pin_read,cia6526_port_changed_fn port_changed);
void cia6526_reset(cia6526 *c);
void cia6526_tick(cia6526 *c,uint64_t cycle);
uint8_t cia6526_read(cia6526 *c,uint8_t reg);
uint8_t cia6526_peek(const cia6526 *c,uint8_t reg);
void cia6526_write(cia6526 *c,uint8_t reg,uint8_t value);
void cia6526_tod_pulse(cia6526 *c);
bool cia6526_irq(const cia6526 *c);
#ifdef __cplusplus
}
#endif
#endif
