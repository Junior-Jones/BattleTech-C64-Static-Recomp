#include "sid6581.h"
#include <limits.h>
#include <string.h>

/* The 16 rate-counter comparison periods used by the NMOS SID envelope
   generator. These are hardware timing facts, not copied implementation code. */
static const uint16_t rate_period[16] = {
    8u,31u,62u,94u,148u,219u,266u,312u,
    391u,976u,1953u,3125u,3906u,11719u,19531u,31250u
};
static const uint8_t sustain_level[16] = {
    0x00u,0x11u,0x22u,0x33u,0x44u,0x55u,0x66u,0x77u,
    0x88u,0x99u,0xaau,0xbbu,0xccu,0xddu,0xeeu,0xffu
};

static int32_t clamp32(int64_t v, int32_t lo, int32_t hi)
{
    if(v < (int64_t)lo) return lo;
    if(v > (int64_t)hi) return hi;
    return (int32_t)v;
}
static int16_t clamp16(int32_t v)
{
    if(v < -32768) return -32768;
    if(v > 32767) return 32767;
    return (int16_t)v;
}
static uint8_t voice_base(unsigned n) { return (uint8_t)(7u*n); }
static unsigned sync_source(unsigned n) { return (n + 2u) % 3u; }
static uint16_t voice_rate_period(const bt_sid6581_voice *v)
{
    unsigned nibble;
    if(v->env_state == BT_SID6581_ENV_ATTACK) nibble=(unsigned)(v->attack_decay>>4);
    else if(v->env_state == BT_SID6581_ENV_DECAY_SUSTAIN) nibble=(unsigned)(v->attack_decay&15u);
    else nibble=(unsigned)(v->sustain_release&15u);
    return rate_period[nibble&15u];
}
static uint8_t exponential_period_for(uint8_t envelope)
{
    if(envelope >= 0x5du) return 1u;
    if(envelope >= 0x36u) return 2u;
    if(envelope >= 0x1au) return 4u;
    if(envelope >= 0x0eu) return 8u;
    if(envelope >= 0x06u) return 16u;
    if(envelope != 0u) return 30u;
    return 1u;
}
static uint16_t noise_output(const bt_sid6581_voice *v)
{
    uint32_t s=v->shift_register;
    return (uint16_t)(((s & 0x100000u)>>9) | ((s & 0x040000u)>>8) |
                      ((s & 0x004000u)>>5) | ((s & 0x000800u)>>3) |
                      ((s & 0x000200u)>>2) | ((s & 0x000020u)<<1) |
                      ((s & 0x000004u)<<3) | ((s & 0x000001u)<<4));
}
static void shift_noise(bt_sid6581_voice *v)
{
    uint32_t bit0=((v->shift_register>>22)^(v->shift_register>>17))&1u;
    v->shift_register=((v->shift_register<<1)|bit0)&0x7fffffu;
}
static void clock_oscillator(bt_sid6581_voice *v)
{
    uint32_t old=v->accumulator;
    uint32_t next;
    v->msb_rising=0u;
    if(v->control & 0x08u){
        v->accumulator=0u;
        v->shift_register=0x7fffffu;
        v->shift_pipeline=0u;
        return;
    }
    next=(old+(uint32_t)v->frequency)&0xffffffu;
    v->accumulator=next;
    if((~old & next & 0x800000u)!=0u)v->msb_rising=1u;
    if((~old & next & 0x080000u)!=0u)v->shift_pipeline=2u;
    else if(v->shift_pipeline!=0u){
        v->shift_pipeline--;
        if(v->shift_pipeline==0u)shift_noise(v);
    }
}
static void synchronize_oscillators(bt_sid6581 *sid)
{
    unsigned n;
    uint8_t reset[3]={0u,0u,0u};
    for(n=0;n<3u;n++){
        unsigned src=sync_source(n);
        if((sid->voice[n].control&0x02u) && sid->voice[src].msb_rising){
            /* If the source is itself being hard-synced on this same edge,
               suppress propagation of that edge. */
            unsigned srcsrc=sync_source(src);
            if(!((sid->voice[src].control&0x02u) && sid->voice[srcsrc].msb_rising))reset[n]=1u;
        }
    }
    for(n=0;n<3u;n++)if(reset[n])sid->voice[n].accumulator=0u;
}
static void envelope_gate(bt_sid6581_voice *v, uint8_t gate)
{
    gate=(uint8_t)(gate!=0u);
    if(v->gate==gate)return;
    v->gate=gate;
    if(gate)v->env_state=BT_SID6581_ENV_ATTACK;
    else v->env_state=BT_SID6581_ENV_RELEASE;
    /* Deliberately do not reset rate_counter: this preserves the classic
       ADSR delay behavior when the active comparison period changes. */
}
static void clock_envelope(bt_sid6581_voice *v)
{
    uint16_t p=voice_rate_period(v);
    v->rate_counter=(uint16_t)((v->rate_counter+1u)&0x7fffu);
    if(v->rate_counter!=p)return;
    v->rate_counter=0u;
    if(v->env_state==BT_SID6581_ENV_ATTACK){
        if(v->envelope<0xffu)v->envelope++;
        if(v->envelope==0xffu){v->env_state=BT_SID6581_ENV_DECAY_SUSTAIN;v->exponential_counter=0u;v->exponential_period=1u;}
        return;
    }
    v->exponential_counter++;
    v->exponential_period=exponential_period_for(v->envelope);
    if(v->exponential_counter<v->exponential_period)return;
    v->exponential_counter=0u;
    if(v->env_state==BT_SID6581_ENV_DECAY_SUSTAIN){
        uint8_t sustain=sustain_level[(v->sustain_release>>4)&15u];
        if(v->envelope>sustain)v->envelope--;
    }else if(v->env_state==BT_SID6581_ENV_RELEASE){
        if(v->envelope>0u)v->envelope--;
    }
}
static uint16_t waveform_output(const bt_sid6581 *sid,unsigned n)
{
    const bt_sid6581_voice *v=&sid->voice[n];
    uint8_t select=(uint8_t)(v->control&0xf0u);
    uint16_t out=0x0fffu;
    uint8_t any=0u;
    if(select&0x10u){
        uint32_t phase=v->accumulator;
        uint32_t src_msb=sid->voice[sync_source(n)].accumulator&0x800000u;
        uint16_t tri=(uint16_t)((phase>>11)&0x0fffu);
        if((phase&0x800000u)^((v->control&0x04u)?src_msb:0u))tri^=0x0fffu;
        out=(uint16_t)(out&tri);any=1u;
    }
    if(select&0x20u){out=(uint16_t)(out&((v->accumulator>>12)&0x0fffu));any=1u;}
    if(select&0x40u){
        uint16_t pw=(uint16_t)(v->pulse_width&0x0fffu);
        uint16_t pulse=(uint16_t)(((v->accumulator>>12)>=pw)?0x0fffu:0u);
        out=(uint16_t)(out&pulse);any=1u;
    }
    if(select&0x80u){out=(uint16_t)(out&noise_output(v));any=1u;}
    /* Multiple 6581 waveforms are analog interactions. C09 declares this
       deterministic digital-AND approximation and tests it as a stable policy. */
    return any?out:0x0800u;
}
static int32_t voice_sample(const bt_sid6581 *sid,unsigned n)
{
    uint16_t w=waveform_output(sid,n);
    int32_t centered=(int32_t)w-2048;
    return (centered*(int32_t)sid->voice[n].envelope)/255;
}
static int32_t render_cycle(bt_sid6581 *sid)
{
    int32_t v[3],direct=0,filtered=0,filter_input=0,selected=0;
    uint8_t route=sid->regs[0x17u]&0x07u;
    uint8_t mode=sid->regs[0x18u];
    uint16_t cutoff=(uint16_t)(((uint16_t)sid->regs[0x16u]<<3)|(sid->regs[0x15u]&7u));
    uint8_t resonance=(uint8_t)(sid->regs[0x17u]>>4);
    unsigned n;
    int32_t f_q15,damp_q14,high;
    for(n=0;n<3u;n++){
        v[n]=voice_sample(sid,n);
        if(route&(1u<<n))filter_input+=v[n];
        else if(!(n==2u && (mode&0x80u)))direct+=v[n];
    }
    /* Project-owned fixed-point state-variable filter. It intentionally
       approximates the analog 6581 filter while preserving deterministic
       cutoff/resonance/routing/mode behavior across hosts. */
    f_q15=256+(int32_t)((uint32_t)cutoff*13760u/2047u);
    damp_q14=12288-(int32_t)resonance*448;
    if(damp_q14<4096)damp_q14=4096;
    high=filter_input-sid->filter_low-(int32_t)(((int64_t)sid->filter_band*damp_q14)>>14);
    sid->filter_band=clamp32((int64_t)sid->filter_band+(((int64_t)high*f_q15)>>15),-131072,131071);
    sid->filter_low=clamp32((int64_t)sid->filter_low+(((int64_t)sid->filter_band*f_q15)>>15),-131072,131071);
    sid->filter_high=clamp32(high,-131072,131071);
    if(mode&0x10u)selected+=sid->filter_low;
    if(mode&0x20u)selected+=sid->filter_band;
    if(mode&0x40u)selected+=sid->filter_high;
    filtered=selected;
    {
        int32_t volume=(int32_t)(mode&15u);
        int32_t digi=volume*96; /* 6581 volume-DAC/digi component. */
        int32_t mixed=((direct+filtered)*volume)/15+digi;
        sid->last_mixed=mixed;
        return mixed;
    }
}
static void update_register_side_effects(bt_sid6581 *sid,uint8_t reg,uint8_t value)
{
    unsigned n;
    for(n=0;n<3u;n++){
        uint8_t b=voice_base(n);
        bt_sid6581_voice *v=&sid->voice[n];
        if(reg==b+0u)v->frequency=(uint16_t)((v->frequency&0xff00u)|value);
        else if(reg==b+1u)v->frequency=(uint16_t)((v->frequency&0x00ffu)|((uint16_t)value<<8));
        else if(reg==b+2u)v->pulse_width=(uint16_t)((v->pulse_width&0x0f00u)|value);
        else if(reg==b+3u)v->pulse_width=(uint16_t)((v->pulse_width&0x00ffu)|(((uint16_t)value&0x0fu)<<8));
        else if(reg==b+4u){v->control=value;envelope_gate(v,(uint8_t)(value&1u));if(value&0x08u){v->accumulator=0u;v->shift_register=0x7fffffu;v->shift_pipeline=0u;}}
        else if(reg==b+5u)v->attack_decay=value;
        else if(reg==b+6u)v->sustain_release=value;
    }
}

void bt_sid6581_reset(bt_sid6581 *sid)
{
    unsigned n;
    uint32_t clock_hz,sample_rate;
    void *ctx;
    bt_sid6581_sample_fn fn;
    if(!sid)return;
    clock_hz=sid->clock_hz?sid->clock_hz:BT_SID6581_PAL_CLOCK_HZ;
    sample_rate=sid->sample_rate?sid->sample_rate:BT_SID6581_DEFAULT_SAMPLE_RATE;
    ctx=sid->sample_ctx;fn=sid->sample_fn;
    memset(sid,0,sizeof(*sid));
    sid->clock_hz=clock_hz;sid->sample_rate=sample_rate;sid->sample_ctx=ctx;sid->sample_fn=fn;
    for(n=0;n<3u;n++){
        sid->voice[n].shift_register=0x7ffffeu;
        sid->voice[n].exponential_period=1u;
        sid->voice[n].env_state=BT_SID6581_ENV_RELEASE;
    }
}
void bt_sid6581_init(bt_sid6581 *sid,uint32_t clock_hz,uint32_t sample_rate,void *sample_ctx,bt_sid6581_sample_fn sample_fn)
{
    if(!sid)return;
    memset(sid,0,sizeof(*sid));
    sid->clock_hz=clock_hz?clock_hz:BT_SID6581_PAL_CLOCK_HZ;
    sid->sample_rate=sample_rate?sample_rate:BT_SID6581_DEFAULT_SAMPLE_RATE;
    sid->sample_ctx=sample_ctx;sid->sample_fn=sample_fn;
    bt_sid6581_reset(sid);
}
void bt_sid6581_set_sample_sink(bt_sid6581 *sid,void *sample_ctx,bt_sid6581_sample_fn sample_fn)
{
    if(!sid)return;
    sid->sample_ctx=sample_ctx;
    sid->sample_fn=sample_fn;
}
bool bt_sid6581_tick(bt_sid6581 *sid,uint64_t cycle)
{
    unsigned n;
    int32_t mixed;
    if(!sid||sid->failed||cycle!=sid->cycle+1u){if(sid)sid->failed=1u;return false;}
    sid->cycle=cycle;
    if(sid->bus_latch_ttl!=0u){sid->bus_latch_ttl--;if(sid->bus_latch_ttl==0u)sid->bus_latch=0u;}
    for(n=0;n<3u;n++)clock_oscillator(&sid->voice[n]);
    synchronize_oscillators(sid);
    for(n=0;n<3u;n++)clock_envelope(&sid->voice[n]);
    mixed=render_cycle(sid);
    sid->sample_accumulator+=sid->sample_rate;
    if(sid->sample_accumulator>=sid->clock_hz){
        sid->sample_accumulator-=sid->clock_hz;
        sid->last_sample=clamp16(mixed*6);
        sid->emitted_samples++;
        if(sid->sample_fn)sid->sample_fn(sid->sample_ctx,cycle,sid->last_sample);
    }
    return true;
}
bool bt_sid6581_advance_to(bt_sid6581 *sid,uint64_t cycle)
{
    if(!sid||sid->failed||cycle<sid->cycle){if(sid)sid->failed=1u;return false;}
    while(sid->cycle<cycle)if(!bt_sid6581_tick(sid,sid->cycle+1u))return false;
    return true;
}
uint8_t bt_sid6581_peek(const bt_sid6581 *sid,uint8_t reg)
{
    if(!sid)return 0xffu;
    reg&=0x1fu;
    if(reg==0x19u||reg==0x1au)return 0xffu; /* target profile has no paddles */
    if(reg==0x1bu)return (uint8_t)(waveform_output(sid,2u)>>4);
    if(reg==0x1cu)return sid->voice[2].envelope;
    return sid->bus_latch;
}
uint8_t bt_sid6581_read(bt_sid6581 *sid,uint8_t reg)
{
    uint8_t v=bt_sid6581_peek(sid,reg);
    if(sid){sid->bus_latch=v;sid->bus_latch_ttl=0x2000u;}
    return v;
}
bool bt_sid6581_write_at(bt_sid6581 *sid,uint64_t cycle,uint8_t reg,uint8_t value)
{
    if(!sid||sid->failed||cycle<sid->cycle){if(sid)sid->failed=1u;return false;}
    if(cycle>sid->cycle && !bt_sid6581_advance_to(sid,cycle))return false;
    reg&=0x1fu;
    sid->regs[reg]=value;
    sid->bus_latch=value;sid->bus_latch_ttl=0x2000u;
    if(reg<=0x14u)update_register_side_effects(sid,reg,value);
    return !sid->failed;
}
