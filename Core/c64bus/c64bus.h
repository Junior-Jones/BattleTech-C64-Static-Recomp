#ifndef BATTLETECH_C64_BUS_H
#define BATTLETECH_C64_BUS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../cpu6510/cpu6510.h"
#ifdef __cplusplus
extern "C" {
#endif

#define C64_BUS_CPU_PORT_FALLOFF_CYCLES 350000u

typedef enum c64_bus_source {
    C64_BUS_SRC_RAM = 0,
    C64_BUS_SRC_BASIC,
    C64_BUS_SRC_KERNAL,
    C64_BUS_SRC_CHARGEN,
    C64_BUS_SRC_IO_VIC,
    C64_BUS_SRC_IO_SID,
    C64_BUS_SRC_COLOR_RAM,
    C64_BUS_SRC_CIA1,
    C64_BUS_SRC_CIA2,
    C64_BUS_SRC_IO1,
    C64_BUS_SRC_IO2,
    C64_BUS_SRC_CART_ROML,
    C64_BUS_SRC_CART_ROMH,
    C64_BUS_SRC_OPEN_BUS,
    C64_BUS_SRC_CPU_PORT_DDR,
    C64_BUS_SRC_CPU_PORT_DATA
} c64_bus_source;

typedef uint8_t (*c64_bus_io_read_fn)(void *ctx, uint16_t canonical_addr);
typedef void (*c64_bus_io_write_fn)(void *ctx, uint16_t canonical_addr, uint8_t value);
typedef uint8_t (*c64_bus_io_peek_fn)(void *ctx, uint16_t canonical_addr);
typedef void (*c64_bus_cart_write_fn)(void *ctx, bool romh, uint16_t addr, uint8_t value);

typedef struct c64_bus_callbacks {
    void *ctx;
    c64_bus_io_read_fn io_read;
    c64_bus_io_write_fn io_write;
    c64_bus_io_peek_fn io_peek;
    c64_bus_cart_write_fn cart_write;
} c64_bus_callbacks;

typedef struct c64_bus {
    uint8_t ram[65536];
    uint8_t color_ram[1024];
    const uint8_t *basic_rom;
    const uint8_t *kernal_rom;
    const uint8_t *chargen_rom;
    const uint8_t *cart_roml;
    const uint8_t *cart_romh;
    uint8_t cpu_port_ddr;
    uint8_t cpu_port_data;
    uint8_t cpu_port_data_out;
    uint8_t cpu_port_cap_67;
    uint64_t cpu_port_cap_deadline_6;
    uint64_t cpu_port_cap_deadline_7;
    uint32_t cpu_port_falloff_cycles;
    uint64_t cpu_cycle;
    bool game_high;
    bool exrom_high;
    uint8_t vic_phi1_bus;
    uint8_t cia2_pra;
    uint8_t cia2_ddra;
    uint8_t vic_bank;
    c64_bus_callbacks cb;
} c64_bus;

void c64_bus_init(c64_bus *b,
                  const uint8_t *basic_rom,
                  const uint8_t *kernal_rom,
                  const uint8_t *chargen_rom,
                  c64_bus_callbacks cb);
void c64_bus_set_cpu_cycle(c64_bus *b, uint64_t cycle);
void c64_bus_set_vic_phi1(c64_bus *b, uint8_t value);
void c64_bus_set_cia2_port_a(c64_bus *b, uint8_t pra, uint8_t ddra);
uint8_t c64_bus_effective_cpu_config(const c64_bus *b);
uint16_t c64_bus_vic_bank_base(const c64_bus *b);

c64_bus_source c64_bus_cpu_source(const c64_bus *b, uint16_t addr);
uint16_t c64_bus_io_canonical_addr(uint16_t addr);

uint8_t c64_bus_cpu_read(c64_bus *b, uint16_t addr);
uint8_t c64_bus_cpu_peek(const c64_bus *b, uint16_t addr);
void c64_bus_cpu_write(c64_bus *b, uint16_t addr, uint8_t value);

uint8_t c64_bus_vic_read(const c64_bus *b, uint16_t vic_addr14);
uint8_t c64_bus_vic_color_read(const c64_bus *b, uint16_t color_index);

#ifdef __cplusplus
}
#endif
#endif
