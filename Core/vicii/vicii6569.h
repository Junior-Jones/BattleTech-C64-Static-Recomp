#ifndef BATTLETECH_C64_VICII6569_H
#define BATTLETECH_C64_VICII6569_H
#include <stdbool.h>
#include <stdint.h>
#include "../c64bus/c64bus.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BT_VICII_PAL_CYCLES_PER_LINE 63u
#define BT_VICII_PAL_LINES_PER_FRAME 312u
#define BT_VICII_PAL_CYCLES_PER_FRAME (BT_VICII_PAL_CYCLES_PER_LINE*BT_VICII_PAL_LINES_PER_FRAME)
#define BT_VICII_NORMAL_WIDTH 384u
#define BT_VICII_NORMAL_HEIGHT 272u
#define BT_VICII_MAX_DMA_SPANS 4u
typedef struct bt_vicii_dma_span { uint64_t first_cycle,last_cycle; } bt_vicii_dma_span;
typedef enum bt_vicii_write_phase {
 BT_VICII_WRITE_NORMAL=0,
 BT_VICII_WRITE_RMW_DUMMY=1,
 BT_VICII_WRITE_RMW_FINAL=2
} bt_vicii_write_phase;
typedef struct bt_vicii_sprite {
 uint16_t x; uint8_t y; uint8_t dma; uint8_t memptr; int8_t memptr_inc; uint8_t exp_flag; uint8_t data[3]; uint8_t next_data[3];
} bt_vicii_sprite;
typedef struct bt_vicii6569 {
 c64_bus *bus;
 uint8_t regs[64];
 uint64_t machine_cycle,frame_number,line_start_cycle;
 uint16_t raster_line,raster_irq_line;
 uint8_t raster_cycle;
 uint8_t allow_bad_lines,badline,display_state,rc;
 uint16_t vc,vcbase;
 uint8_t mem_counter_inc,memory_fetch_done,force_display_state,ycounter_reset_checked,buf_offset;
 uint8_t matrix[40],matrix_color[40];
 uint8_t irq_flags,irq_mask,irq_line,raster_line0_irq_pending;
 uint8_t sprite_dma_mask,sprite_fetch_mask,sprite_display_mask;
 bt_vicii_sprite sprites[8];
 bt_vicii_dma_span sprite_spans[BT_VICII_MAX_DMA_SPANS];
 uint8_t sprite_span_count;
 uint8_t ba_low,cpu_read_stolen;
 uint8_t border_vertical,border_main;
 uint16_t display_ystart,display_ystop;
 uint16_t display_xstart,display_xstop,next_display_xstart,next_display_xstop;
 uint8_t display_xstart_pending,display_xstop_pending,blank_this_line,open_left_border,open_right_border,xsmooth;
 uint8_t framebuffer[BT_VICII_NORMAL_WIDTH*BT_VICII_NORMAL_HEIGHT];
 uint64_t completed_frames;
} bt_vicii6569;
void bt_vicii6569_init(bt_vicii6569 *v,c64_bus *bus);
void bt_vicii6569_tick(bt_vicii6569 *v,uint64_t machine_cycle);
uint8_t bt_vicii6569_read(bt_vicii6569 *v,uint8_t reg);
uint8_t bt_vicii6569_peek(const bt_vicii6569 *v,uint8_t reg);
void bt_vicii6569_write_phase(bt_vicii6569 *v,uint8_t reg,uint8_t value,bt_vicii_write_phase phase);
bool bt_vicii6569_irq(const bt_vicii6569 *v);
bool bt_vicii6569_cpu_read_stolen(const bt_vicii6569 *v);
uint16_t bt_vicii6569_screen_base(const bt_vicii6569 *v);
uint16_t bt_vicii6569_char_base(const bt_vicii6569 *v);
uint16_t bt_vicii6569_bitmap_base(const bt_vicii6569 *v);
const uint8_t *bt_vicii6569_framebuffer(const bt_vicii6569 *v);
#ifdef __cplusplus
}
#endif
#endif
