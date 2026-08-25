#include "c64_machine.h"
#include "../drive1541/bt_1541_target.h"
#include <limits.h>
#include <string.h>

static uint8_t cia1_pins(void *ctx,unsigned port,uint8_t pra,uint8_t ddra,uint8_t prb,uint8_t ddrb)
{
    bt_c64_machine *m=(bt_c64_machine*)ctx;
    return c64_input_cia1_pins(&m->input,port,pra,ddra,prb,ddrb);
}

static uint8_t cia2_pins(void *ctx,unsigned port,uint8_t pra,uint8_t ddra,uint8_t prb,uint8_t ddrb)
{
    bt_c64_machine *m=(bt_c64_machine*)ctx;
    (void)pra;(void)ddra;(void)prb;(void)ddrb;
    if(port)return m->cia2_pb_input;
    if(m->iec){uint8_t ie=bt_iec_c64_cia2_inputs(m->iec);return (uint8_t)((m->cia2_pa_input&0x3fu)|(ie&0xc0u));}
    return m->cia2_pa_input;
}

static void cia1_changed(void *ctx,unsigned port,uint8_t latch,uint8_t ddr,uint8_t driven)
{
    (void)ctx;(void)port;(void)latch;(void)ddr;(void)driven;
}

static void cia2_changed(void *ctx,unsigned port,uint8_t latch,uint8_t ddr,uint8_t driven)
{
    bt_c64_machine *m=(bt_c64_machine*)ctx;
    if(port==0u){c64_bus_set_cia2_port_a(&m->bus,latch,ddr);if(m->iec)bt_iec_set_c64_cia2_pa(m->iec,latch,ddr);}
    else{m->userport_pb_output=driven;m->userport_pb_ddr=ddr;}
}

static uint8_t io_read(void *ctx,uint16_t addr)
{
    bt_c64_machine *m=(bt_c64_machine*)ctx;
    if(addr>=0xd000u && addr<=0xd03fu)return bt_vicii6569_read(&m->vicii,(uint8_t)addr);
    if(addr>=0xd400u && addr<=0xd41fu)return bt_sid6581_read(&m->sid,(uint8_t)addr);
    if(addr>=0xdc00u && addr<=0xdc0fu)return cia6526_read(&m->cia1,(uint8_t)addr);
    if(addr>=0xdd00u && addr<=0xdd0fu)return cia6526_read(&m->cia2,(uint8_t)addr);
    return m->bus.vic_phi1_bus;
}

static uint8_t io_peek(void *ctx,uint16_t addr)
{
    bt_c64_machine *m=(bt_c64_machine*)ctx;
    if(addr>=0xd000u && addr<=0xd03fu)return bt_vicii6569_peek(&m->vicii,(uint8_t)addr);
    if(addr>=0xd400u && addr<=0xd41fu)return bt_sid6581_peek(&m->sid,(uint8_t)addr);
    if(addr>=0xdc00u && addr<=0xdc0fu)return cia6526_peek(&m->cia1,(uint8_t)addr);
    if(addr>=0xdd00u && addr<=0xdd0fu)return cia6526_peek(&m->cia2,(uint8_t)addr);
    return m->bus.vic_phi1_bus;
}

static void io_write(void *ctx,uint16_t addr,uint8_t value)
{
    bt_c64_machine *m=(bt_c64_machine*)ctx;
    if(addr>=0xd000u && addr<=0xd03fu)bt_vicii6569_write_phase(&m->vicii,(uint8_t)addr,value,(bt_vicii_write_phase)m->cpu_write_phase);
    else if(addr>=0xd400u && addr<=0xd41fu){if(!bt_sid6581_write_at(&m->sid,m->scheduler.cycle,(uint8_t)addr,value))m->failed=1u;}
    else if(addr>=0xdc00u && addr<=0xdc0fu)cia6526_write(&m->cia1,(uint8_t)addr,value);
    else if(addr>=0xdd00u && addr<=0xdd0fu)cia6526_write(&m->cia2,(uint8_t)addr,value);
}

static void sched_event(void *ctx,uint64_t cycle,uint32_t type,uint64_t value)
{
    bt_c64_machine *m=(bt_c64_machine*)ctx;
    (void)cycle;
    switch(type){
        case BT_C64_EVENT_JOYSTICK2:c64_input_set_joystick(&m->input,2u,(uint8_t)value);break;
        case BT_C64_EVENT_JOYSTICK1:c64_input_set_joystick(&m->input,1u,(uint8_t)value);break;
        case BT_C64_EVENT_KEY:c64_input_set_key(&m->input,(uint8_t)((value>>8)&7u),(uint8_t)(value&7u),((value>>16)&1u)!=0u);break;
        case BT_C64_EVENT_RESTORE:c64_input_set_restore(&m->input,value!=0u);m->restore_line=(uint8_t)(value!=0u);break;
        case BT_C64_EVENT_USERPORT_PB:m->cia2_pb_input=(uint8_t)value;break;
        case BT_C64_EVENT_CIA2_PA_INPUT:m->cia2_pa_input=(uint8_t)value;break;
        default:m->failed=1u;break;
    }
}

void bt_c64_machine_recompute_interrupts(bt_c64_machine *m)
{
    bool irq,nmi;
    if(!m)return;
    irq=cia6526_irq(&m->cia1)||bt_vicii6569_irq(&m->vicii)||m->external_irq;
    nmi=cia6526_irq(&m->cia2)||m->external_nmi||m->restore_line;
    cpu6510_set_irq(&m->cpu,irq);
    cpu6510_set_nmi(&m->cpu,nmi);
}

static void sched_tick(void *ctx,uint64_t cycle)
{
    bt_c64_machine *m=(bt_c64_machine*)ctx;
    bt_vicii6569_tick(&m->vicii,cycle);
    if(!bt_sid6581_tick(&m->sid,cycle)){m->failed=1u;return;}
    cia6526_tick(&m->cia1,cycle);
    cia6526_tick(&m->cia2,cycle);
    if(m->drive1541&&!bt_1541_target_advance_to_c64_cycle(m->drive1541,cycle)){m->failed=1u;return;}
    m->tod_accumulator+=m->power_hz;
    if(m->tod_accumulator>=m->cpu_hz){
        m->tod_accumulator-=m->cpu_hz;
        cia6526_tod_pulse(&m->cia1);
        cia6526_tod_pulse(&m->cia2);
    }
    bt_c64_machine_recompute_interrupts(m);
}

static bool cpu_kind_reads(cpu6510_bus_kind kind)
{
    return kind==CPU6510_BUS_OPCODE_READ || kind==CPU6510_BUS_READ || kind==CPU6510_BUS_DUMMY_READ;
}

static bool logical_to_physical(bt_c64_machine *m,uint64_t logical,uint64_t *physical)
{
    if(logical>UINT64_MAX-m->cpu_stall_cycles){m->failed=1u;return false;}
    *physical=logical+m->cpu_stall_cycles;
    return true;
}

static bool advance_cpu_slot(bt_c64_machine *m,uint64_t logical,cpu6510_bus_kind kind)
{
    uint64_t physical;
    if(!logical_to_physical(m,logical,&physical))return false;
    if(!bt_c64_machine_advance_to(m,physical))return false;
    if(cpu_kind_reads(kind)){
        while(bt_vicii6569_cpu_read_stolen(&m->vicii)){
            if(m->cpu_stall_cycles==UINT64_MAX || physical==UINT64_MAX){m->failed=1u;return false;}
            m->cpu_stall_cycles++;
            m->vic_read_stall_cycles++;
            physical++;
            if(!bt_c64_machine_advance_to(m,physical))return false;
        }
    }
    m->last_cpu_physical_cycle=physical;
    c64_bus_set_cpu_cycle(&m->bus,physical);
    return true;
}

static void cpu_begin(void *ctx,uint64_t cycle,uint16_t addr,cpu6510_bus_kind kind)
{
    bt_c64_machine *m=(bt_c64_machine*)ctx;
    (void)addr;
    if(!advance_cpu_slot(m,cycle,kind))m->failed=1u;
}

static uint8_t cpu_read(void *ctx,uint16_t addr,cpu6510_bus_kind kind)
{
    (void)kind;
    return c64_bus_cpu_read(&((bt_c64_machine*)ctx)->bus,addr);
}

static void cpu_write(void *ctx,uint16_t addr,uint8_t value,cpu6510_bus_kind kind)
{
    bt_c64_machine *m=(bt_c64_machine*)ctx;
    if(kind==CPU6510_BUS_DUMMY_WRITE){
        if(m->cpu_rmw_pending){m->failed=1u;return;}
        m->cpu_rmw_pending=1u;
        m->cpu_rmw_addr=addr;
        m->cpu_write_phase=BT_VICII_WRITE_RMW_DUMMY;
    }else if(kind==CPU6510_BUS_WRITE && m->cpu_rmw_pending){
        if(m->cpu_rmw_addr!=addr){m->failed=1u;return;}
        m->cpu_write_phase=BT_VICII_WRITE_RMW_FINAL;
    }else{
        m->cpu_write_phase=BT_VICII_WRITE_NORMAL;
    }
    c64_bus_cpu_write(&m->bus,addr,value);
    if(m->cpu_write_phase==BT_VICII_WRITE_RMW_FINAL)m->cpu_rmw_pending=0u;
    m->cpu_write_phase=BT_VICII_WRITE_NORMAL;
}

void bt_c64_machine_init(bt_c64_machine *m,const uint8_t *basic,const uint8_t *kernal,const uint8_t *chargen)
{
    c64_bus_callbacks cb;
    cpu6510_bus cp;
    if(!m)return;
    memset(m,0,sizeof(*m));
    m->cpu_hz=BT_C64_PAL_CPU_HZ;
    m->power_hz=BT_C64_PAL_POWER_HZ;
    m->cia2_pa_input=0xffu;
    m->cia2_pb_input=0xffu;
    c64_input_init(&m->input);
    cia6526_init(&m->cia1,m,cia1_pins,cia1_changed);
    cia6526_init(&m->cia2,m,cia2_pins,cia2_changed);
    memset(&cb,0,sizeof(cb));
    cb.ctx=m;cb.io_read=io_read;cb.io_write=io_write;cb.io_peek=io_peek;
    c64_bus_init(&m->bus,basic,kernal,chargen,cb);
    bt_vicii6569_init(&m->vicii,&m->bus);
    bt_vicii6569_tick(&m->vicii,0u);
    bt_sid6581_init(&m->sid,BT_C64_PAL_CPU_HZ,BT_SID6581_DEFAULT_SAMPLE_RATE,NULL,NULL);
    bt_scheduler_init(&m->scheduler,m,sched_event);
    bt_scheduler_add_ticker(&m->scheduler,m,sched_tick);
    cp.ctx=m;cp.begin_cycle=cpu_begin;cp.read=cpu_read;cp.write=cpu_write;
    cpu6510_init(&m->cpu,cp);
    c64_bus_set_cpu_cycle(&m->bus,0u);
    bt_c64_machine_recompute_interrupts(m);
}

bool bt_c64_machine_advance_to(bt_c64_machine *m,uint64_t cycle)
{
    if(!m||m->failed)return false;
    if(!bt_scheduler_advance_to(&m->scheduler,cycle)){m->failed=1u;return false;}
    m->last_rendezvous_cycle=cycle;
    return true;
}

bool bt_c64_machine_schedule_input(bt_c64_machine *m,uint64_t cycle,uint16_t priority,uint32_t type,uint64_t value)
{
    if(!m||m->failed)return false;
    if(!bt_scheduler_schedule(&m->scheduler,cycle,priority,type,value))return false;
    /* Host input is normally timestamped at the most recently published core
       cycle.  A same-cycle event must become observable immediately; otherwise
       the scheduler's next forward advance starts at cycle+1 and the event can
       never be visited.  advance_to(current) processes only due events and does
       not tick target hardware, preserving deterministic emulated time. */
    if(cycle==m->scheduler.cycle)return bt_scheduler_advance_to(&m->scheduler,cycle);
    return true;
}

bool bt_c64_machine_attach_1541(bt_c64_machine *m,bt_iec_bus *iec,struct bt_1541_target *drive)
{
    if(!m||!iec||!drive||m->failed)return false;
    m->iec=iec;m->drive1541=drive;
    bt_iec_set_c64_cia2_pa(iec,m->cia2.pra,m->cia2.ddra);
    return !bt_1541_target_failed(drive);
}

bool bt_c64_machine_aot_rendezvous(void *ctx,uint64_t cpu_cycle)
{
    bt_c64_machine *m=(bt_c64_machine*)ctx;
    uint64_t physical;
    if(!m||m->failed)return false;
    if(!logical_to_physical(m,cpu_cycle,&physical))return false;
    if(!bt_c64_machine_advance_to(m,physical))return false;

    /* Every AOT block begins with an opcode read.  Pre-stalling that read here
       is deliberate: an IRQ/NMI that becomes active while BA holds the CPU can
       be observed by cpu6510_step_fixed() before the opcode is admitted. */
    while(bt_vicii6569_cpu_read_stolen(&m->vicii)){
        if(m->cpu_stall_cycles==UINT64_MAX || physical==UINT64_MAX){m->failed=1u;return false;}
        m->cpu_stall_cycles++;
        m->vic_read_stall_cycles++;
        physical++;
        if(!bt_c64_machine_advance_to(m,physical))return false;
    }
    m->last_cpu_physical_cycle=physical;
    c64_bus_set_cpu_cycle(&m->bus,physical);
    bt_c64_machine_recompute_interrupts(m);
    return !m->failed;
}
