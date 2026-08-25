#ifndef BATTLETECH_C64_BT_IEC_H
#define BATTLETECH_C64_BT_IEC_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct bt_iec_bus {
 uint8_t c64_atn_low,c64_clk_low,c64_data_low;
 uint8_t drive_clk_low,drive_data_out,drive_data_low,drive_atna;
 uint8_t atn,clk,data;
} bt_iec_bus;
void bt_iec_init(bt_iec_bus *b);
void bt_iec_set_c64_cia2_pa(bt_iec_bus *b,uint8_t latch,uint8_t ddr);
void bt_iec_set_drive_outputs(bt_iec_bus *b,bool data_out_bit,bool clk_out_bit,bool atna_bit);
uint8_t bt_iec_c64_cia2_inputs(const bt_iec_bus *b);
uint8_t bt_iec_drive_via1_pb_inputs(const bt_iec_bus *b,unsigned device_number);
bool bt_iec_atn_high(const bt_iec_bus *b);
#ifdef __cplusplus
}
#endif
#endif
