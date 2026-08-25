#include "Core/cpu6510/cpu6510.h"

#include <stdio.h>
#include <string.h>

typedef struct bus_event {
    uint64_t cycle;
    uint16_t address;
    cpu6510_bus_kind kind;
} bus_event;

typedef struct fixture {
    uint8_t memory[65536];
    bus_event events[16];
    size_t event_count;
    cpu6510 cpu;
} fixture;

static int failures;

#define CHECK(condition, message) do { \
    if(condition) printf("PASS %s\n", message); \
    else { printf("FAIL %s\n", message); failures++; } \
} while(0)

static void begin_cycle(void *ctx,uint64_t cycle,uint16_t address,cpu6510_bus_kind kind){
    fixture *f=(fixture*)ctx;
    if(f->event_count<sizeof(f->events)/sizeof(f->events[0])){
        bus_event *event=&f->events[f->event_count++];
        event->cycle=cycle;
        event->address=address;
        event->kind=kind;
    }
}

static uint8_t read_byte(void *ctx,uint16_t address,cpu6510_bus_kind kind){
    fixture *f=(fixture*)ctx;
    (void)kind;
    return f->memory[address];
}

static void write_byte(void *ctx,uint16_t address,uint8_t value,cpu6510_bus_kind kind){
    fixture *f=(fixture*)ctx;
    (void)kind;
    f->memory[address]=value;
}

static void init_fixture(fixture *f,uint16_t pc,uint8_t sp,uint8_t status){
    cpu6510_bus bus;
    memset(f,0,sizeof(*f));
    bus.ctx=f;
    bus.begin_cycle=begin_cycle;
    bus.read=read_byte;
    bus.write=write_byte;
    cpu6510_init(&f->cpu,bus);
    f->cpu.pc=pc;
    f->cpu.sp=sp;
    f->cpu.cycles=100u;
    cpu6510_set_status(&f->cpu,status);
}

static void check_bus_event(const fixture *f,size_t index,uint16_t address,cpu6510_bus_kind kind,const char *message){
    CHECK(index<f->event_count&&f->events[index].address==address&&f->events[index].kind==kind,message);
}

static void test_irq_sequence(void){
    fixture f;
    cpu6510_step_result result;
    init_fixture(&f,0x3456u,0xfdu,CPU6510_P_U|CPU6510_P_C);
    f.memory[0xfffeu]=0x78u;
    f.memory[0xffffu]=0x56u;
    cpu6510_set_irq(&f.cpu,true);
    result=cpu6510_service_pending_interrupt(&f.cpu);

    CHECK(result.serviced_irq&&!result.serviced_nmi,"IRQ is serviced");
    CHECK(result.stop_reason==CPU6510_STOP_NONE,"IRQ reports no stop");
    CHECK(result.cycles==7u&&f.cpu.cycles==107u,"IRQ consumes seven bus cycles");
    CHECK(f.cpu.pc==0x5678u,"IRQ loads the FFFE/FFFF vector");
    CHECK(f.cpu.sp==0xfau,"IRQ pushes three stack bytes");
    CHECK(f.memory[0x01fdu]==0x34u&&f.memory[0x01fcu]==0x56u,"IRQ pushes PC high then PC low");
    CHECK(f.memory[0x01fbu]==(CPU6510_P_U|CPU6510_P_C),"IRQ pushes status with B clear and U set");
    CHECK((f.cpu.p&CPU6510_P_I)!=0u&&f.cpu.irq_sampled,"IRQ sets and samples the interrupt-disable flag");
    CHECK(f.event_count==7u,"IRQ exposes exactly seven bus events");
    check_bus_event(&f,0u,0x3456u,CPU6510_BUS_DUMMY_READ,"IRQ cycle 1 reads the interrupted PC");
    check_bus_event(&f,1u,0x3456u,CPU6510_BUS_DUMMY_READ,"IRQ cycle 2 reads the interrupted PC");
    check_bus_event(&f,2u,0x01fdu,CPU6510_BUS_WRITE,"IRQ cycle 3 pushes PC high");
    check_bus_event(&f,3u,0x01fcu,CPU6510_BUS_WRITE,"IRQ cycle 4 pushes PC low");
    check_bus_event(&f,4u,0x01fbu,CPU6510_BUS_WRITE,"IRQ cycle 5 pushes processor status");
    check_bus_event(&f,5u,0xfffeu,CPU6510_BUS_READ,"IRQ cycle 6 reads vector low");
    check_bus_event(&f,6u,0xffffu,CPU6510_BUS_READ,"IRQ cycle 7 reads vector high");
}

static void test_nmi_priority(void){
    fixture f;
    cpu6510_step_result result;
    init_fixture(&f,0xabcdu,0xf0u,CPU6510_P_U);
    f.memory[0xfffau]=0x34u;
    f.memory[0xfffbu]=0x12u;
    f.memory[0xfffeu]=0x78u;
    f.memory[0xffffu]=0x56u;
    cpu6510_set_irq(&f.cpu,true);
    cpu6510_set_nmi(&f.cpu,true);
    result=cpu6510_service_pending_interrupt(&f.cpu);

    CHECK(result.serviced_nmi&&!result.serviced_irq,"NMI has priority over simultaneous IRQ");
    CHECK(f.cpu.pc==0x1234u,"NMI loads the FFFA/FFFB vector");
    CHECK(!f.cpu.nmi_edge_pending,"NMI edge is consumed exactly once");
    check_bus_event(&f,5u,0xfffau,CPU6510_BUS_READ,"NMI reads vector low from FFFA");
    check_bus_event(&f,6u,0xfffbu,CPU6510_BUS_READ,"NMI reads vector high from FFFB");
}

static void test_nmi_ignores_i_flag(void){
    fixture f;
    cpu6510_step_result result;
    init_fixture(&f,0x2000u,0xe0u,CPU6510_P_U|CPU6510_P_I);
    f.memory[0xfffau]=0x00u;
    f.memory[0xfffbu]=0x40u;
    cpu6510_set_nmi(&f.cpu,true);
    result=cpu6510_service_pending_interrupt(&f.cpu);
    CHECK(result.serviced_nmi&&f.cpu.pc==0x4000u,"NMI is serviced while I is set");
}

static void test_rejected_calls_do_not_execute(void){
    fixture f;
    cpu6510_step_result result;
    init_fixture(&f,0x4567u,0xd0u,CPU6510_P_U);
    f.memory[0x4567u]=0xeau;
    result=cpu6510_service_pending_interrupt(&f.cpu);
    CHECK(result.stop_reason==CPU6510_STOP_INTERRUPT_NOT_PENDING,"no-pending call is rejected");
    CHECK(f.cpu.pc==0x4567u&&f.cpu.cycles==100u&&f.event_count==0u,"no-pending call cannot fetch or execute an opcode");

    init_fixture(&f,0x4567u,0xd0u,CPU6510_P_U|CPU6510_P_I);
    cpu6510_set_irq(&f.cpu,true);
    result=cpu6510_service_pending_interrupt(&f.cpu);
    CHECK(result.stop_reason==CPU6510_STOP_INTERRUPT_NOT_PENDING,"masked IRQ is not serviced");
    CHECK(f.cpu.pc==0x4567u&&f.cpu.cycles==100u&&f.event_count==0u,"masked IRQ performs no bus cycle");

    init_fixture(&f,0x4567u,0xd0u,CPU6510_P_U);
    f.cpu.jammed=true;
    cpu6510_set_irq(&f.cpu,true);
    result=cpu6510_service_pending_interrupt(&f.cpu);
    CHECK(result.stop_reason==CPU6510_STOP_JAM,"jammed CPU rejects interrupt entry");
    CHECK(f.cpu.pc==0x4567u&&f.cpu.cycles==100u&&f.event_count==0u,"jammed CPU performs no bus cycle");
}

int main(void){
    test_irq_sequence();
    test_nmi_priority();
    test_nmi_ignores_i_flag();
    test_rejected_calls_do_not_execute();
    if(failures){
        printf("CPU6510_INTERRUPT_TEST FAIL failures=%d\n",failures);
        return 1;
    }
    printf("CPU6510_INTERRUPT_TEST PASS\n");
    return 0;
}
