#include "c64bus.h"
#include <string.h>

typedef enum cart_mode { CART_NONE=0, CART_8K, CART_16K, CART_ULTIMAX } cart_mode;

static cart_mode mode(const c64_bus *b) {
    if (b->game_high && b->exrom_high) return CART_NONE;
    if (b->game_high && !b->exrom_high) return CART_8K;
    if (!b->game_high && !b->exrom_high) return CART_16K;
    return CART_ULTIMAX;
}

void c64_bus_init(c64_bus *b, const uint8_t *basic_rom, const uint8_t *kernal_rom,
                  const uint8_t *chargen_rom, c64_bus_callbacks cb) {
    memset(b, 0, sizeof(*b));
    b->basic_rom = basic_rom;
    b->kernal_rom = kernal_rom;
    b->chargen_rom = chargen_rom;
    /* Standard C64 6510 port reset: DDR=DATA=DATA_OUT=0. The physical
       LORAM/HIRAM/CHAREN/sense pullups are resolved by port_data_read(). */
    b->cpu_port_falloff_cycles = C64_BUS_CPU_PORT_FALLOFF_CYCLES;
    b->game_high = true;
    b->exrom_high = true;
    b->vic_phi1_bus = 0xff;
    b->cb = cb;
    c64_bus_set_cia2_port_a(b, 0, 0);
}

void c64_bus_set_cpu_cycle(c64_bus *b, uint64_t cycle) { b->cpu_cycle = cycle; }
void c64_bus_set_vic_phi1(c64_bus *b, uint8_t value) { b->vic_phi1_bus = value; }
void c64_bus_set_cia2_port_a(c64_bus *b, uint8_t pra, uint8_t ddra) {
    uint8_t pins = (uint8_t)(pra | (uint8_t)~ddra);
    b->cia2_pra = pra;
    b->cia2_ddra = ddra;
    b->vic_bank = (uint8_t)((~pins) & 3u);
}
uint8_t c64_bus_effective_cpu_config(const c64_bus *b) {
    return (uint8_t)(((uint8_t)~b->cpu_port_ddr | b->cpu_port_data) & 7u);
}
uint16_t c64_bus_vic_bank_base(const c64_bus *b) { return (uint16_t)((uint16_t)b->vic_bank << 14); }

static c64_bus_source io_source(uint16_t addr) {
    if (addr <= 0xd3ff) return C64_BUS_SRC_IO_VIC;
    if (addr <= 0xd7ff) return C64_BUS_SRC_IO_SID;
    if (addr <= 0xdbff) return C64_BUS_SRC_COLOR_RAM;
    if (addr <= 0xdcff) return C64_BUS_SRC_CIA1;
    if (addr <= 0xddff) return C64_BUS_SRC_CIA2;
    if (addr <= 0xdeff) return C64_BUS_SRC_IO1;
    return C64_BUS_SRC_IO2;
}

c64_bus_source c64_bus_cpu_source(const c64_bus *b, uint16_t addr) {
    uint8_t cfg;
    bool loram, hiram, charen;
    cart_mode m;
    if (addr == 0) return C64_BUS_SRC_CPU_PORT_DDR;
    if (addr == 1) return C64_BUS_SRC_CPU_PORT_DATA;
    m = mode(b);
    if (m == CART_ULTIMAX) {
        if (addr < 0x1000) return C64_BUS_SRC_RAM;
        if (addr < 0x8000) return C64_BUS_SRC_OPEN_BUS;
        if (addr < 0xa000) return C64_BUS_SRC_CART_ROML;
        if (addr < 0xd000) return C64_BUS_SRC_OPEN_BUS;
        if (addr < 0xe000) return io_source(addr);
        return C64_BUS_SRC_CART_ROMH;
    }
    cfg = c64_bus_effective_cpu_config(b);
    loram = (cfg & 1u) != 0;
    hiram = (cfg & 2u) != 0;
    charen = (cfg & 4u) != 0;
    if (addr >= 0x8000 && addr < 0xa000) {
        if ((m == CART_8K || m == CART_16K) && loram && hiram) return C64_BUS_SRC_CART_ROML;
        return C64_BUS_SRC_RAM;
    }
    if (addr >= 0xa000 && addr < 0xc000) {
        if (m == CART_16K && hiram) return C64_BUS_SRC_CART_ROMH;
        if ((m == CART_NONE || m == CART_8K) && loram && hiram) return C64_BUS_SRC_BASIC;
        return C64_BUS_SRC_RAM;
    }
    if (addr >= 0xd000 && addr < 0xe000) {
        /* In 16K cartridge mode the PLA's CHARGEN selection differs from
           the no-cart/8K case: with CHAREN=0, HIRAM must be asserted.
           I/O selection with CHAREN=1 still occurs when LORAM or HIRAM is set. */
        if (charen) return (loram || hiram) ? io_source(addr) : C64_BUS_SRC_RAM;
        if (m == CART_16K) return hiram ? C64_BUS_SRC_CHARGEN : C64_BUS_SRC_RAM;
        return (loram || hiram) ? C64_BUS_SRC_CHARGEN : C64_BUS_SRC_RAM;
    }
    if (addr >= 0xe000 && hiram) return C64_BUS_SRC_KERNAL;
    return C64_BUS_SRC_RAM;
}

uint16_t c64_bus_io_canonical_addr(uint16_t addr) {
    if (addr >= 0xd000 && addr <= 0xd3ff) return (uint16_t)(0xd000u | (addr & 0x3fu));
    if (addr >= 0xd400 && addr <= 0xd7ff) return (uint16_t)(0xd400u | (addr & 0x1fu));
    if (addr >= 0xdc00 && addr <= 0xdcff) return (uint16_t)(0xdc00u | (addr & 0x0fu));
    if (addr >= 0xdd00 && addr <= 0xddff) return (uint16_t)(0xdd00u | (addr & 0x0fu));
    return addr;
}

static uint8_t port_data_read(const c64_bus *b) {
    /* Standard C64 board, no cassette attached. This follows the 6510/PLA
       electrical contract used by the pinned oracle: pullups on bits 0,1,2,4;
       bit 5 is low as an input; bits 6/7 are floating and retain output charge
       for a deterministic nominal 350000 CPU cycles before falling low. */
    uint8_t v = (uint8_t)((b->cpu_port_data | (uint8_t)~b->cpu_port_ddr) &
                          (b->cpu_port_data_out | 0x17u));
    if (!(b->cpu_port_ddr & 0x20u)) v &= (uint8_t)~0x20u;
    if (!(b->cpu_port_ddr & 0x40u)) {
        v &= (uint8_t)~0x40u;
        if ((b->cpu_port_cap_67 & 0x40u) && b->cpu_cycle <= b->cpu_port_cap_deadline_6) v |= 0x40u;
    }
    if (!(b->cpu_port_ddr & 0x80u)) {
        v &= (uint8_t)~0x80u;
        if ((b->cpu_port_cap_67 & 0x80u) && b->cpu_cycle <= b->cpu_port_cap_deadline_7) v |= 0x80u;
    }
    return v;
}
static void port_refresh_data_out(c64_bus *b) {
    b->cpu_port_data_out = (uint8_t)((b->cpu_port_data_out & (uint8_t)~b->cpu_port_ddr) |
                                     (b->cpu_port_data & b->cpu_port_ddr));
}
static void port_charge(c64_bus *b, uint8_t bit, uint8_t value) {
    if (value & bit) b->cpu_port_cap_67 |= bit; else b->cpu_port_cap_67 &= (uint8_t)~bit;
    uint64_t deadline = (b->cpu_cycle > UINT64_MAX - b->cpu_port_falloff_cycles)
                      ? UINT64_MAX : b->cpu_cycle + b->cpu_port_falloff_cycles;
    if (bit == 0x40u) b->cpu_port_cap_deadline_6 = deadline;
    else b->cpu_port_cap_deadline_7 = deadline;
}
static uint8_t rombyte(const uint8_t *p, size_t off) { return p ? p[off] : 0xff; }
static uint8_t fallback_cia2_peek(const c64_bus *b, uint16_t canonical) {
    if (canonical == 0xdd00) return (uint8_t)(b->cia2_pra | (uint8_t)~b->cia2_ddra);
    if (canonical == 0xdd02) return b->cia2_ddra;
    return b->vic_phi1_bus;
}
static uint8_t io_peek_impl(const c64_bus *b, uint16_t addr) {
    c64_bus_source s = io_source(addr);
    uint16_t c = c64_bus_io_canonical_addr(addr);
    if (s == C64_BUS_SRC_COLOR_RAM) return (uint8_t)((b->vic_phi1_bus & 0xf0u) | (b->color_ram[addr & 0x3ffu] & 0x0fu));
    if (b->cb.io_peek) return b->cb.io_peek(b->cb.ctx, c);
    if (s == C64_BUS_SRC_CIA2) return fallback_cia2_peek(b, c);
    return b->vic_phi1_bus;
}
static uint8_t io_read_impl(c64_bus *b, uint16_t addr) {
    c64_bus_source s = io_source(addr);
    uint16_t c = c64_bus_io_canonical_addr(addr);
    if (s == C64_BUS_SRC_COLOR_RAM) return (uint8_t)((b->vic_phi1_bus & 0xf0u) | (b->color_ram[addr & 0x3ffu] & 0x0fu));
    if (b->cb.io_read) return b->cb.io_read(b->cb.ctx, c);
    if (s == C64_BUS_SRC_CIA2) return fallback_cia2_peek(b, c);
    return b->vic_phi1_bus;
}

uint8_t c64_bus_cpu_peek(const c64_bus *b, uint16_t addr) {
    c64_bus_source s = c64_bus_cpu_source(b, addr);
    switch (s) {
        case C64_BUS_SRC_CPU_PORT_DDR: return b->cpu_port_ddr;
        case C64_BUS_SRC_CPU_PORT_DATA: return port_data_read(b);
        case C64_BUS_SRC_RAM: return b->ram[addr];
        case C64_BUS_SRC_BASIC: return rombyte(b->basic_rom, addr - 0xa000u);
        case C64_BUS_SRC_KERNAL: return rombyte(b->kernal_rom, addr - 0xe000u);
        case C64_BUS_SRC_CHARGEN: return rombyte(b->chargen_rom, addr & 0x0fffu);
        case C64_BUS_SRC_CART_ROML: return rombyte(b->cart_roml, addr - 0x8000u);
        case C64_BUS_SRC_CART_ROMH: return rombyte(b->cart_romh, addr >= 0xe000 ? addr - 0xe000u : addr - 0xa000u);
        case C64_BUS_SRC_OPEN_BUS: return b->vic_phi1_bus;
        default: return io_peek_impl(b, addr);
    }
}
uint8_t c64_bus_cpu_read(c64_bus *b, uint16_t addr) {
    c64_bus_source s = c64_bus_cpu_source(b, addr);
    switch (s) {
        case C64_BUS_SRC_CPU_PORT_DDR: return b->cpu_port_ddr;
        case C64_BUS_SRC_CPU_PORT_DATA: return port_data_read(b);
        case C64_BUS_SRC_RAM: return b->ram[addr];
        case C64_BUS_SRC_BASIC: return rombyte(b->basic_rom, addr - 0xa000u);
        case C64_BUS_SRC_KERNAL: return rombyte(b->kernal_rom, addr - 0xe000u);
        case C64_BUS_SRC_CHARGEN: return rombyte(b->chargen_rom, addr & 0x0fffu);
        case C64_BUS_SRC_CART_ROML: return rombyte(b->cart_roml, addr - 0x8000u);
        case C64_BUS_SRC_CART_ROMH: return rombyte(b->cart_romh, addr >= 0xe000 ? addr - 0xe000u : addr - 0xa000u);
        case C64_BUS_SRC_OPEN_BUS: return b->vic_phi1_bus;
        default: return io_read_impl(b, addr);
    }
}

static void io_write_impl(c64_bus *b, uint16_t addr, uint8_t value) {
    c64_bus_source s = io_source(addr);
    uint16_t c = c64_bus_io_canonical_addr(addr);
    if (s == C64_BUS_SRC_COLOR_RAM) {
        b->color_ram[addr & 0x3ffu] = (uint8_t)(value & 0x0fu);
        return;
    }
    if (s == C64_BUS_SRC_CIA2) {
        if (c == 0xdd00) c64_bus_set_cia2_port_a(b, value, b->cia2_ddra);
        else if (c == 0xdd02) c64_bus_set_cia2_port_a(b, b->cia2_pra, value);
    }
    if (b->cb.io_write) b->cb.io_write(b->cb.ctx, c, value);
}
void c64_bus_cpu_write(c64_bus *b, uint16_t addr, uint8_t value) {
    c64_bus_source s = c64_bus_cpu_source(b, addr);
    cart_mode m = mode(b);
    if (addr == 0 || addr == 1) {
        /* The CPU port wins address decode; the DRAM byte underneath captures the VIC phi-1 bus. */
        b->ram[addr] = b->vic_phi1_bus;
        if (addr == 0) {
            uint8_t old = b->cpu_port_ddr;
            if ((old & 0x40u) && !(value & 0x40u)) port_charge(b,0x40u,b->cpu_port_data);
            if ((old & 0x80u) && !(value & 0x80u)) port_charge(b,0x80u,b->cpu_port_data);
            b->cpu_port_ddr = value;
            port_refresh_data_out(b);
        } else {
            if (b->cpu_port_ddr & 0x40u) port_charge(b,0x40u,value);
            if (b->cpu_port_ddr & 0x80u) port_charge(b,0x80u,value);
            b->cpu_port_data = value;
            port_refresh_data_out(b);
        }
        return;
    }
    switch (s) {
        case C64_BUS_SRC_RAM: b->ram[addr] = value; break;
        case C64_BUS_SRC_BASIC:
        case C64_BUS_SRC_KERNAL:
        case C64_BUS_SRC_CHARGEN:
            b->ram[addr] = value; break;
        case C64_BUS_SRC_CART_ROML:
        case C64_BUS_SRC_CART_ROMH:
            if (m != CART_ULTIMAX) b->ram[addr] = value;
            if (b->cb.cart_write) b->cb.cart_write(b->cb.ctx, s == C64_BUS_SRC_CART_ROMH, addr, value);
            break;
        case C64_BUS_SRC_OPEN_BUS:
            break;
        default:
            io_write_impl(b, addr, value); break;
    }
}

uint8_t c64_bus_vic_read(const c64_bus *b, uint16_t a14) {
    uint16_t local = (uint16_t)(a14 & 0x3fffu);
    uint16_t physical = (uint16_t)(c64_bus_vic_bank_base(b) | local);
    if ((b->vic_bank == 0 || b->vic_bank == 2) && local >= 0x1000 && local < 0x2000)
        return rombyte(b->chargen_rom, local - 0x1000u);
    return b->ram[physical];
}
uint8_t c64_bus_vic_color_read(const c64_bus *b, uint16_t index) { return (uint8_t)(b->color_ram[index & 0x3ffu] & 0x0fu); }
