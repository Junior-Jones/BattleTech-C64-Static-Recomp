#ifndef BATTLETECH_C64_MACHINE_H
#define BATTLETECH_C64_MACHINE_H
#include <stdbool.h>
#include <stdint.h>
#include "../cpu6510/cpu6510.h"
#include "../c64bus/c64bus.h"
#include "../scheduler/bt_scheduler.h"
#include "../cia/cia6526.h"
#include "../input/c64_input.h"
#include "../vicii/vicii6569.h"
#include "../sid/sid6581.h"
#include "../drive1541/bt_iec.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BT_C64_PAL_CPU_HZ 985248u
#define BT_C64_PAL_POWER_HZ 50u
enum {BT_C64_EVENT_JOYSTICK2=1,BT_C64_EVENT_JOYSTICK1=2,BT_C64_EVENT_KEY=3,BT_C64_EVENT_RESTORE=4,BT_C64_EVENT_USERPORT_PB=5,BT_C64_EVENT_CIA2_PA_INPUT=6};
struct bt_1541_target;
typedef struct bt_c64_machine {
 bt_scheduler scheduler;
 c64_bus bus;
 cpu6510 cpu;
 cia6526 cia1,cia2;
 c64_input input;
 bt_vicii6569 vicii;
 bt_sid6581 sid;
 bt_iec_bus *iec;
 struct bt_1541_target *drive1541;
 uint8_t cia2_pa_input,cia2_pb_input;
 uint8_t userport_pb_output,userport_pb_ddr;
 uint8_t external_irq,external_nmi;
 uint8_t restore_line;
 uint32_t cpu_hz,power_hz;
 uint32_t tod_accumulator;
 uint64_t last_rendezvous_cycle;
 uint64_t cpu_stall_cycles;
 uint64_t last_cpu_physical_cycle;
 uint64_t vic_read_stall_cycles;
 uint16_t cpu_rmw_addr;
 uint8_t cpu_rmw_pending;
 uint8_t cpu_write_phase;
 uint8_t failed;
} bt_c64_machine;
void bt_c64_machine_init(bt_c64_machine *m,const uint8_t *basic,const uint8_t *kernal,const uint8_t *chargen);
bool bt_c64_machine_advance_to(bt_c64_machine *m,uint64_t cycle);
bool bt_c64_machine_schedule_input(bt_c64_machine *m,uint64_t cycle,uint16_t priority,uint32_t type,uint64_t value);
void bt_c64_machine_recompute_interrupts(bt_c64_machine *m);
bool bt_c64_machine_aot_rendezvous(void *ctx,uint64_t cpu_cycle);
bool bt_c64_machine_attach_1541(bt_c64_machine *m,bt_iec_bus *iec,struct bt_1541_target *drive);
#ifdef __cplusplus
}
#endif
#endif
