#ifndef BATTLETECH_C64_CPU6510_H
#define BATTLETECH_C64_CPU6510_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum {
    CPU6510_P_C=0x01, CPU6510_P_Z=0x02, CPU6510_P_I=0x04, CPU6510_P_D=0x08,
    CPU6510_P_B=0x10, CPU6510_P_U=0x20, CPU6510_P_V=0x40, CPU6510_P_N=0x80
};

typedef enum cpu6510_bus_kind {
    CPU6510_BUS_OPCODE_READ=0,
    CPU6510_BUS_READ,
    CPU6510_BUS_DUMMY_READ,
    CPU6510_BUS_WRITE,
    CPU6510_BUS_DUMMY_WRITE
} cpu6510_bus_kind;

typedef void (*cpu6510_begin_cycle_fn)(void*,uint64_t,uint16_t,cpu6510_bus_kind);
typedef uint8_t (*cpu6510_read_fn)(void*,uint16_t,cpu6510_bus_kind);
typedef void (*cpu6510_write_fn)(void*,uint16_t,uint8_t,cpu6510_bus_kind);

typedef struct cpu6510_bus {
    void *ctx;
    cpu6510_begin_cycle_fn begin_cycle;
    cpu6510_read_fn read;
    cpu6510_write_fn write;
} cpu6510_bus;

typedef enum cpu6510_stop_reason {
    CPU6510_STOP_NONE=0,
    CPU6510_STOP_JAM=1,
    CPU6510_STOP_AOT_PC_MISMATCH=2,
    CPU6510_STOP_AOT_SOURCE_MISMATCH=3,
    CPU6510_STOP_AOT_INTERRUPT_PENDING=4,
    CPU6510_STOP_INTERRUPT_NOT_PENDING=5
} cpu6510_stop_reason;

typedef struct cpu6510 {
    uint16_t pc;
    uint8_t a,x,y,sp,p;
    uint64_t cycles;
    bool jammed;
    bool irq_line;
    bool nmi_line;
    bool nmi_edge_pending;
    bool irq_sampled;
    uint8_t xaa_magic;
    uint8_t lxa_magic;
    cpu6510_bus bus;
} cpu6510;

typedef struct cpu6510_step_result {
    uint8_t opcode;
    uint16_t opcode_pc;
    uint32_t cycles;
    cpu6510_stop_reason stop_reason;
    bool serviced_irq;
    bool serviced_nmi;
} cpu6510_step_result;

void cpu6510_init(cpu6510*,cpu6510_bus);
void cpu6510_set_irq(cpu6510*,bool);
void cpu6510_set_nmi(cpu6510*,bool);
uint32_t cpu6510_reset(cpu6510*);
/* Services only an already-sampled hardware interrupt. This entry never
   fetches or decodes an opcode. NMI has priority over IRQ, matching the
   NMOS 6502/6510 hardware contract. */
cpu6510_step_result cpu6510_service_pending_interrupt(cpu6510*);
/* C06 AOT entry: executes a compile-time selected opcode at an exact guest PC.
   The opcode bus cycle is still performed and its byte must equal expected_opcode.
   Operand bytes remain bus-visible so C05-approved operand-only SMC generations can
   reuse the same statically selected semantic block without runtime opcode decode.
   Pending IRQ/NMI is returned as an explicit rendezvous instead of being serviced. */
cpu6510_step_result cpu6510_step_fixed(cpu6510*,uint16_t expected_pc,uint8_t expected_opcode);
void cpu6510_set_status(cpu6510*,uint8_t);

#ifdef __cplusplus
}
#endif
#endif
