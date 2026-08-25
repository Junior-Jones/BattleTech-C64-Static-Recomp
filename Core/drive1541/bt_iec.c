#include "bt_iec.h"
#include <string.h>
static void resolve(bt_iec_bus*b){
 if(!b)return;
 /* The 1541 ATNA XOR gate is electrical/combinational: a change on C64 ATN
    changes the drive DATA contribution immediately, without waiting for a
    subsequent VIA write. Keep the raw PB1 data output separately so every
    resolve recomputes the gated DATA-low contribution. */
 b->drive_data_low=(uint8_t)((b->drive_data_out!=0u) || ((b->drive_atna!=0u) ^ (b->c64_atn_low!=0u)));
 b->atn=(uint8_t)!b->c64_atn_low;
 b->clk=(uint8_t)!(b->c64_clk_low||b->drive_clk_low);
 b->data=(uint8_t)!(b->c64_data_low||b->drive_data_low);
}
void bt_iec_init(bt_iec_bus*b){if(!b)return;memset(b,0,sizeof(*b));resolve(b);}
void bt_iec_set_c64_cia2_pa(bt_iec_bus*b,uint8_t latch,uint8_t ddr){if(!b)return;/* C64 PA3..5 pass through inverting open-collector drivers: logical 1 with DDR output asserts line low. */b->c64_atn_low=(uint8_t)(((ddr&0x08u)&&(latch&0x08u))!=0u);b->c64_clk_low=(uint8_t)(((ddr&0x10u)&&(latch&0x10u))!=0u);b->c64_data_low=(uint8_t)(((ddr&0x20u)&&(latch&0x20u))!=0u);resolve(b);}
void bt_iec_set_drive_outputs(bt_iec_bus*b,bool data_out_bit,bool clk_out_bit,bool atna_bit){if(!b)return;/* 1541 VIA1 PB1/PB3 are inverted at the bus. ATNA participates in the 1541 combinational data-output gate during ATN. */b->drive_data_out=(uint8_t)data_out_bit;b->drive_atna=(uint8_t)atna_bit;b->drive_clk_low=(uint8_t)clk_out_bit;resolve(b);}
uint8_t bt_iec_c64_cia2_inputs(const bt_iec_bus*b){uint8_t v=0xffu;if(!b)return v;if(!b->clk)v&=(uint8_t)~0x40u;if(!b->data)v&=(uint8_t)~0x80u;return v;}
uint8_t bt_iec_drive_via1_pb_inputs(const bt_iec_bus*b,unsigned device){uint8_t v=0;if(!b)return 0xffu;/* 1541 input buffers are inverted: the resolved DATA/CLOCK/ATN lines read as 1 when asserted low. PB0 sees the actual resolved DATA line, including DATA pulled low by this drive's own ATNA gate; PB5/PB6 are address straps. */if(!b->data)v|=0x01u;if(!b->clk)v|=0x04u;v|=(uint8_t)(((device-8u)&3u)<<5);if(!b->atn)v|=0x80u;return v;}
bool bt_iec_atn_high(const bt_iec_bus*b){return b&&b->atn;}
