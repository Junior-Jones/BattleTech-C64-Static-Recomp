#ifndef BATTLETECH_C64_BT_VIA6522_H
#define BATTLETECH_C64_BT_VIA6522_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {BT_VIA_IFR_CA2=0x01,BT_VIA_IFR_CA1=0x02,BT_VIA_IFR_SR=0x04,BT_VIA_IFR_CB2=0x08,BT_VIA_IFR_CB1=0x10,BT_VIA_IFR_T2=0x20,BT_VIA_IFR_T1=0x40};
typedef uint8_t (*bt_via_port_read_fn)(void *ctx,unsigned port,uint8_t latch,uint8_t ddr);
typedef void (*bt_via_port_write_fn)(void *ctx,unsigned port,uint8_t latch,uint8_t ddr,uint8_t driven);
typedef void (*bt_via_control_write_fn)(void *ctx,unsigned pin,bool level);
typedef struct bt_via6522 {uint8_t orb,ora,ddrb,ddra,sr,acr,pcr,ifr,ier;uint16_t t1c,t1l,t2c;uint8_t t1_running,t2_running,t1_fired,t2_fired,pb7,ca1,cb1,ca2,cb2,unsupported;uint64_t cycle;void*io_ctx;bt_via_port_read_fn port_read;bt_via_port_write_fn port_write;bt_via_control_write_fn control_write;} bt_via6522;
void bt_via6522_init(bt_via6522 *v,void *ctx,bt_via_port_read_fn rd,bt_via_port_write_fn wr,bt_via_control_write_fn cw);
void bt_via6522_reset(bt_via6522 *v);
void bt_via6522_tick(bt_via6522 *v,uint64_t cycle);
uint8_t bt_via6522_read(bt_via6522 *v,uint8_t reg);
uint8_t bt_via6522_peek(const bt_via6522 *v,uint8_t reg);
void bt_via6522_write(bt_via6522 *v,uint8_t reg,uint8_t value);
void bt_via6522_set_ca1(bt_via6522 *v,bool level);
bool bt_via6522_irq(const bt_via6522 *v);
#ifdef __cplusplus
}
#endif
#endif
