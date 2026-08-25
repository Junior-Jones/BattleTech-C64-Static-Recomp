#include "vicii6569.h"
#include <string.h>

#define FIRST_DMA_LINE 0x30u
#define LAST_DMA_LINE 0xf7u
#define MATRIX_FETCH_CYCLE 11u
#define MATRIX_STEAL_CYCLES 43u
#define SPRITE_FETCH_CYCLE 54u
#define TEXT_COLS 40u

/* C08 project-owned sprite Y-expansion crunch contract. Values are signed:
   the source proof includes negative increments and therefore memptr_inc must
   never be represented as an unsigned byte. */
static const int8_t sprite_crunch_table[64]={
  1,4,3,4,1,0,-1,0,1,4,3,4,1,8,7,8,
  1,4,3,4,1,0,-1,0,1,4,3,4,1,-8,-9,-8,
  1,4,3,4,1,0,-1,0,1,4,3,4,1,8,7,8,
  1,4,3,4,1,0,-1,0,1,4,3,4,1,-40,-41,0
};

static bool in_span(uint64_t cycle,const bt_vicii_dma_span *span)
{
    return cycle>=span->first_cycle && cycle<=span->last_cycle;
}

static bool read_steal_class_active(const bt_vicii6569 *v,uint64_t cycle)
{
    unsigned i;
    if(v->badline){
        uint64_t first=v->line_start_cycle+MATRIX_FETCH_CYCLE;
        uint64_t last=first+MATRIX_STEAL_CYCLES-1u;
        if(cycle>=first && cycle<=last)return true;
    }
    for(i=0;i<v->sprite_span_count;i++)if(in_span(cycle,&v->sprite_spans[i]))return true;
    return false;
}

static void irq_recompute(bt_vicii6569 *v)
{
    v->irq_line=(uint8_t)((v->irq_flags & v->irq_mask & 0x0fu)!=0u);
}

static void set_irq(bt_vicii6569 *v,uint8_t bit)
{
    v->irq_flags|=(uint8_t)(bit & 0x0fu);
    irq_recompute(v);
}

uint16_t bt_vicii6569_screen_base(const bt_vicii6569 *v)
{
    return (uint16_t)((v->regs[0x18] & 0xf0u)<<6);
}

uint16_t bt_vicii6569_char_base(const bt_vicii6569 *v)
{
    return (uint16_t)((v->regs[0x18] & 0x0eu)<<10);
}

uint16_t bt_vicii6569_bitmap_base(const bt_vicii6569 *v)
{
    return (uint16_t)((v->regs[0x18] & 0x08u)<<10);
}

static bool badline_condition(const bt_vicii6569 *v,uint8_t d011)
{
    return v->raster_line>=FIRST_DMA_LINE
        && v->raster_line<=LAST_DMA_LINE
        && v->allow_bad_lines
        && ((v->raster_line & 7u)==(d011 & 7u));
}

static void sprite_spans(bt_vicii6569 *v,uint64_t line_start,uint8_t mask)
{
    unsigned n;
    uint64_t lo=0,hi=0;
    bool have=false;
    v->sprite_span_count=0;
    for(n=0;n<8u;n++){
        if(mask & (1u<<n)){
            uint64_t first=line_start+SPRITE_FETCH_CYCLE+2u*n;
            uint64_t last=first+4u;
            if(have && first<=hi+1u){
                if(last>hi)hi=last;
            }else{
                if(have && v->sprite_span_count<BT_VICII_MAX_DMA_SPANS){
                    v->sprite_spans[v->sprite_span_count].first_cycle=lo;
                    v->sprite_spans[v->sprite_span_count].last_cycle=hi;
                    v->sprite_span_count++;
                }
                lo=first;
                hi=last;
                have=true;
            }
        }
    }
    if(have && v->sprite_span_count<BT_VICII_MAX_DMA_SPANS){
        v->sprite_spans[v->sprite_span_count].first_cycle=lo;
        v->sprite_spans[v->sprite_span_count].last_cycle=hi;
        v->sprite_span_count++;
    }
}

static void check_sprite_dma(bt_vicii6569 *v)
{
    unsigned n;
    uint8_t new_mask=v->sprite_dma_mask;
    for(n=0;n<8u;n++){
        uint8_t bit=(uint8_t)(1u<<n);
        bt_vicii_sprite *s=&v->sprites[n];
        bool visible=(v->regs[0x15] & bit)!=0u;
        if(visible && s->y==(uint8_t)v->raster_line && !s->dma){
            s->dma=1u;
            s->memptr=0u;
            s->exp_flag=(uint8_t)((v->regs[0x17] & bit)?0u:1u);
            s->memptr_inc=(int8_t)(s->exp_flag?3:0);
            new_mask|=bit;
        }else if(s->dma){
            s->memptr=(uint8_t)(((int)s->memptr+(int)s->memptr_inc)&0x3f);
            if(v->regs[0x17] & bit)s->exp_flag^=1u;
            s->memptr_inc=(int8_t)(s->exp_flag?3:0);
            if(s->memptr==63u){
                s->dma=0u;
                new_mask&=(uint8_t)~bit;
                if(visible && s->y==(uint8_t)v->raster_line){
                    s->dma=1u;
                    s->memptr=0u;
                    s->exp_flag=(uint8_t)((v->regs[0x17] & bit)?0u:1u);
                    s->memptr_inc=(int8_t)(s->exp_flag?3:0);
                    new_mask|=bit;
                }
            }
        }
    }
    v->sprite_dma_mask=new_mask;
    v->sprite_fetch_mask=new_mask;
    sprite_spans(v,v->line_start_cycle,new_mask);
}

static void fetch_sprite_data(bt_vicii6569 *v)
{
    unsigned n;
    uint16_t screen=bt_vicii6569_screen_base(v);
    if(!v->bus)return;
    for(n=0;n<8u;n++){
        uint8_t bit=(uint8_t)(1u<<n);
        bt_vicii_sprite *s=&v->sprites[n];
        if(v->sprite_fetch_mask & bit){
            uint8_t pointer=c64_bus_vic_read(v->bus,(uint16_t)(screen+0x03f8u+n));
            uint16_t base=(uint16_t)((uint16_t)pointer<<6);
            unsigned k;
            for(k=0;k<3u;k++)s->next_data[k]=c64_bus_vic_read(v->bus,(uint16_t)(base+((s->memptr+k)&0x3fu)));
        }
    }
}

static void fetch_matrix_range(bt_vicii6569 *v,unsigned offs,unsigned num,unsigned num_ff)
{
    unsigned i;
    uint16_t screen=bt_vicii6569_screen_base(v);
    if(!v->bus || offs>=TEXT_COLS)return;
    if(num>TEXT_COLS-offs)num=TEXT_COLS-offs;
    if(num_ff>num)num_ff=num;
    for(i=0;i<num;i++){
        unsigned dst=offs+i;
        if(i<num_ff){
            v->matrix[dst]=0xffu;
            v->matrix_color[dst]=(uint8_t)(v->bus->vic_phi1_bus&0x0fu);
        }else{
            uint16_t index=(uint16_t)((v->vcbase+dst)&0x03ffu);
            v->matrix[dst]=c64_bus_vic_read(v->bus,(uint16_t)(screen+index));
            v->matrix_color[dst]=(uint8_t)(c64_bus_vic_color_read(v->bus,index)&0x0fu);
        }
    }
    v->memory_fetch_done=1u;
}

static void fetch_matrix(bt_vicii6569 *v)
{
    v->vc=v->vcbase;
    v->buf_offset=0u;
    fetch_matrix_range(v,0u,TEXT_COLS,0u);
}

static void line_start(bt_vicii6569 *v)
{
    unsigned n;
    uint8_t den=(uint8_t)((v->regs[0x11] & 0x10u)!=0u);
    /* MOS 6569 VC/VCBASE frame contract: outside the bad-line range,
       VCBASE is reset once per frame.  Use raster line 0 as the stable
       project-owned reset point.  Without this reset a normal 25-row
       display advances VCBASE by 1000 cells every frame (equivalent to
       -24 modulo 1024), causing deterministic title-screen tiling. */
    if(v->raster_line==0u){
        v->vcbase=0u;
        v->vc=0u;
    }
    if(v->display_xstart_pending){v->display_xstart=v->next_display_xstart;v->display_xstart_pending=0u;}
    if(v->display_xstop_pending){v->display_xstop=v->next_display_xstop;v->display_xstop_pending=0u;}
    v->blank_this_line=0u;v->open_left_border=v->open_right_border;v->open_right_border=0u;v->buf_offset=0u;
    v->badline=0u;
    v->sprite_display_mask=v->sprite_fetch_mask;
    for(n=0;n<8u;n++)memcpy(v->sprites[n].data,v->sprites[n].next_data,3u);
    v->memory_fetch_done=0u;
    v->ycounter_reset_checked=0u;
    if(v->raster_line==FIRST_DMA_LINE)v->allow_bad_lines=den;
    if(badline_condition(v,v->regs[0x11])){
        v->badline=1u;
        v->display_state=1u;
        v->rc=0u;
        v->ycounter_reset_checked=1u;
        v->mem_counter_inc=TEXT_COLS;
    }
    /* PAL line 0 has the documented one-cycle raster IRQ offset. Keep the
       pending event separate from the compare register so a cycle-0 compare
       rewrite cannot accidentally create a second same-frame assertion. */
    v->raster_line0_irq_pending=0u;
    if(v->raster_line==v->raster_irq_line){
        if(v->raster_line==0u)v->raster_line0_irq_pending=1u;
        else set_irq(v,0x01u);
    }

    if(v->raster_line==v->display_ystop)v->border_vertical=1u;
    if(den && v->raster_line==v->display_ystart)v->border_vertical=0u;
}

static void line_finish(bt_vicii6569 *v)
{
    if(v->display_state){
        if(v->rc==7u){
            v->display_state=0u;
            v->vcbase=(uint16_t)((v->vcbase+TEXT_COLS)&0x03ffu);
            v->vc=v->vcbase;
        }else{
            v->rc=(uint8_t)((v->rc+1u)&7u);
        }
    }
    if(v->force_display_state){
        v->display_state=1u;
        v->force_display_state=0u;
    }
    v->badline=0u;
    v->ycounter_reset_checked=0u;
    v->memory_fetch_done=0u;
}

static void d011_border_transition(bt_vicii6569 *v,uint8_t old_value,uint8_t new_value)
{
    uint8_t old25=(uint8_t)((old_value&0x08u)!=0u);
    uint8_t new25=(uint8_t)((new_value&0x08u)!=0u);
    uint8_t old_den=(uint8_t)((old_value&0x10u)!=0u);
    unsigned line=v->raster_line,cycle=v->raster_cycle;
    if(old25!=new25){
        if(new25){
            v->display_ystart=0x33u;v->display_ystop=0xfbu;
            if(line==0xf7u && cycle>0u)v->border_vertical=1u;
            else if(old_den && cycle>0u && (line==0x37u || line==0x33u))v->border_vertical=0u;
        }else{
            v->display_ystart=0x37u;v->display_ystop=0xf7u;
            if(old_den && line==0x33u && cycle>0u)v->border_vertical=0u;
            else if(line==0xfbu && cycle>0u)v->border_vertical=1u;
        }
    }
    /* If the display flip-flop was already open on the selected upper
       border line, clearing DEN in this same write does not close it. */
    if(old_den && line==v->display_ystart && cycle>0u)v->border_vertical=0u;
}

static void d011_badline_transition(bt_vicii6569 *v,uint8_t old_value,uint8_t new_value)
{
    bool old_bad=v->badline!=0u;
    bool new_bad;
    bool was_idle=v->display_state==0u;
    (void)old_value;
    if(v->raster_line==FIRST_DMA_LINE){
        if(v->raster_cycle==0u)v->allow_bad_lines=(uint8_t)((new_value & 0x10u)!=0u);
        else if(new_value & 0x10u)v->allow_bad_lines=1u;
    }else if(v->raster_line==FIRST_DMA_LINE+1u && v->raster_cycle==0u && (new_value&0x10u)){
        /* A cycle-0 write on the line immediately after FIRST_DMA_LINE still
           sees FIRST_DMA_LINE at machine_cycle-1 and therefore latches DEN. */
        v->allow_bad_lines=1u;
    }
    new_bad=badline_condition(v,new_value);
    if(old_bad && !new_bad){
        if(v->raster_cycle<MATRIX_FETCH_CYCLE)v->badline=0u;
        /* A line that became bad dynamically can leave display state active
           even after a later D011 write makes it good again. */
        if(v->raster_cycle>0u)v->display_state=1u;
        if(v->raster_cycle>MATRIX_FETCH_CYCLE+2u && !v->ycounter_reset_checked){
            v->rc=0u;v->ycounter_reset_checked=1u;
        }
        return;
    }
    if(!old_bad && new_bad){
        unsigned c=v->raster_cycle;
        if(c>=MATRIX_FETCH_CYCLE && c<MATRIX_FETCH_CYCLE+TEXT_COLS+3u){
            int xpos=(int)c-(int)(MATRIX_FETCH_CYCLE+3u);
            int num=(int)TEXT_COLS-xpos;
            unsigned pos,ff=3u,inc;
            v->badline=1u;
            if(c<=MATRIX_FETCH_CYCLE+2u){v->rc=0u;v->ycounter_reset_checked=1u;}
            if(num>(int)TEXT_COLS){num=(int)TEXT_COLS;pos=0u;inc=TEXT_COLS;ff=c-MATRIX_FETCH_CYCLE;}
            else if(was_idle){pos=0u;inc=(unsigned)(num<0?0:num);if(xpos>0)v->buf_offset=(uint8_t)xpos;}
            else{pos=(unsigned)(xpos<0?0:(xpos>39?39:xpos));inc=TEXT_COLS;}
            if(num<0)num=0;
            v->vc=v->vcbase;v->mem_counter_inc=(uint8_t)inc;
            if(num>0)fetch_matrix_range(v,pos,(unsigned)num,ff);
            v->display_state=1u;v->ycounter_reset_checked=1u;
        }else if(c<=MATRIX_FETCH_CYCLE+TEXT_COLS+6u){
            v->badline=1u;
            if(was_idle && c>=MATRIX_FETCH_CYCLE)v->mem_counter_inc=0u;
            v->display_state=1u;v->ycounter_reset_checked=1u;
        }else{
            v->force_display_state=1u;
            if(c==MATRIX_FETCH_CYCLE+TEXT_COLS+7u)v->mem_counter_inc=0u;
            v->ycounter_reset_checked=1u;
        }
    }
}

static void raster_compare_changed(bt_vicii6569 *v,uint16_t old_line)
{
    if(v->raster_irq_line==old_line)return;
    if(v->raster_line==0u && v->raster_cycle==0u){
        /* Changing away cancels the delayed cycle-1 event. Changing into
           line 0 asserts immediately and schedules only the next frame. */
        v->raster_line0_irq_pending=0u;
    }
    if(v->raster_irq_line==v->raster_line)set_irq(v,0x01u);
}

static void d015_store(bt_vicii6569 *v,uint8_t value)
{
    uint8_t old=v->regs[0x15u];
    uint8_t newly_enabled=(uint8_t)((value^old)&value);
    v->regs[0x15u]=value;
    /* The 6569 checks sprite DMA at cycle 54 and has a second check at
       cycle 55 when a sprite is newly enabled exactly there. Writes before
       cycle 54 are consumed by the ordinary scheduled check; later writes
       wait for the next line. */
    if(v->raster_cycle==SPRITE_FETCH_CYCLE+1u && newly_enabled){
        check_sprite_dma(v);
        fetch_sprite_data(v);
    }
}

static void d016_store(bt_vicii6569 *v,uint8_t value)
{
    uint8_t old=v->regs[0x16u];
    uint8_t old40=(uint8_t)((old&0x08u)!=0u),new40=(uint8_t)((value&0x08u)!=0u);
    uint16_t start=(uint16_t)(new40?32u:39u),stop=(uint16_t)(new40?352u:343u);
    if(old40!=new40){
        if(v->raster_cycle<=17u)v->display_xstart=start;
        else{v->next_display_xstart=start;v->display_xstart_pending=1u;}
        if(v->raster_cycle<=56u)v->display_xstop=stop;
        else{v->next_display_xstop=stop;v->display_xstop_pending=1u;}
        if(new40 && !old40 && v->raster_cycle==17u)v->blank_this_line=1u;
        if(old40 && !new40 && v->raster_cycle==56u && (v->open_left_border || (!v->border_vertical && v->raster_line!=v->display_ystop)))v->open_right_border=1u;
    }
    v->xsmooth=(uint8_t)(value&7u);
    v->regs[0x16u]=value;
}

static void d017_store(bt_vicii6569 *v,uint8_t value)
{
    unsigned n;
    uint8_t old=v->regs[0x17u];
    if(old==value)return;
    for(n=0;n<8u;n++){
        uint8_t bit=(uint8_t)(1u<<n);
        bt_vicii_sprite *sp=&v->sprites[n];
        uint8_t was=(uint8_t)((old&bit)!=0u),now=(uint8_t)((value&bit)!=0u);
        if(was && !now && !sp->exp_flag){
            if(v->raster_cycle==15u)sp->memptr_inc=sprite_crunch_table[sp->memptr&0x3fu];
            else if(v->raster_cycle<15u || v->raster_cycle>=SPRITE_FETCH_CYCLE)sp->memptr_inc=3;
            /* Cycles 16..53 deliberately retain the current increment. */
            sp->exp_flag=1u;
        }
        /* Enabling Y expansion has no immediate side effect. */
    }
    v->regs[0x17u]=value;
}

static void d019_store(bt_vicii6569 *v,uint8_t value,bt_vicii_write_phase phase)
{
    /* Both halves of a 6510 RMW are real bus writes. The machine layer
       preserves DUMMY versus FINAL so this routine can remain explicit about
       the two-write contract while applying the same acknowledge rule to
       each physical write value. Scheduler/VIC events for the cycle have
       already been processed before either bus write reaches this function. */
    if(phase!=BT_VICII_WRITE_NORMAL && phase!=BT_VICII_WRITE_RMW_DUMMY && phase!=BT_VICII_WRITE_RMW_FINAL)return;
    v->irq_flags&=(uint8_t)~(value&0x0fu);
    irq_recompute(v);
}

typedef struct bt_vicii_pixel_sample { uint8_t color,opaque; } bt_vicii_pixel_sample;

static bt_vicii_pixel_sample graphics_sample(const bt_vicii6569 *v,unsigned x)
{
    bt_vicii_pixel_sample out;
    int gx=(int)x-32-(int)v->xsmooth;
    unsigned col,px;
    uint8_t mode,screen,color,data;
    uint16_t cell,addr;
    out.color=(uint8_t)(v->regs[0x21]&0x0fu);out.opaque=0u;
    if(!v->display_state || gx<0 || gx>=320)return out;
    col=(unsigned)gx>>3;
    px=(unsigned)gx&7u;
    {
        int bi=(int)col-(int)v->buf_offset;
        if(bi<0 || bi>=(int)TEXT_COLS)return out;
        screen=v->matrix[(unsigned)bi];
        color=(uint8_t)(v->matrix_color[(unsigned)bi]&0x0fu);
    }
    cell=(uint16_t)((v->vcbase+col)&0x03ffu);
    mode=(uint8_t)(((v->regs[0x11]&0x40u)?4u:0u)|((v->regs[0x11]&0x20u)?2u:0u)|((v->regs[0x16]&0x10u)?1u:0u));
    if(mode>=5u){out.color=0u;return out;}
    if(mode==2u || mode==3u){
        addr=(uint16_t)(bt_vicii6569_bitmap_base(v)+((cell<<3)&0x1ff8u)+v->rc);
        data=c64_bus_vic_read(v->bus,addr);
        if(mode==2u){
            out.opaque=(uint8_t)((data>>(7u-px))&1u);
            out.color=(uint8_t)(out.opaque?(screen>>4):(screen&0x0fu));
            return out;
        }
        {
            unsigned pair=px>>1;
            uint8_t code=(uint8_t)((data>>(6u-2u*pair))&3u);
            out.opaque=(uint8_t)(code!=0u);
            if(code==0u)out.color=(uint8_t)(v->regs[0x21]&0x0fu);
            else if(code==1u)out.color=(uint8_t)(screen>>4);
            else if(code==2u)out.color=(uint8_t)(screen&0x0fu);
            else out.color=color;
            return out;
        }
    }
    if(mode==4u){
        uint8_t bgsel=(uint8_t)(screen>>6);
        addr=(uint16_t)(bt_vicii6569_char_base(v)+((uint16_t)(screen&0x3fu)<<3)+v->rc);
        data=c64_bus_vic_read(v->bus,addr);
        out.opaque=(uint8_t)((data>>(7u-px))&1u);
        out.color=(uint8_t)(out.opaque?color:(v->regs[0x21u+bgsel]&0x0fu));
        return out;
    }
    addr=(uint16_t)(bt_vicii6569_char_base(v)+((uint16_t)screen<<3)+v->rc);
    data=c64_bus_vic_read(v->bus,addr);
    if(mode==1u && (color&0x08u)){
        unsigned pair=px>>1;
        uint8_t code=(uint8_t)((data>>(6u-2u*pair))&3u);
        out.opaque=(uint8_t)(code!=0u);
        if(code==0u)out.color=(uint8_t)(v->regs[0x21]&0x0fu);
        else if(code==1u)out.color=(uint8_t)(v->regs[0x22]&0x0fu);
        else if(code==2u)out.color=(uint8_t)(v->regs[0x23]&0x0fu);
        else out.color=(uint8_t)(color&0x07u);
        return out;
    }
    out.opaque=(uint8_t)((data>>(7u-px))&1u);
    out.color=(uint8_t)(out.opaque?color:(v->regs[0x21]&0x0fu));
    return out;
}

static bool sprite_sample(const bt_vicii6569 *v,unsigned n,unsigned x,uint8_t *color)
{
    const bt_vicii_sprite *s=&v->sprites[n];
    uint8_t bit=(uint8_t)(1u<<n);
    unsigned width=(v->regs[0x1d]&bit)?48u:24u;
    unsigned dx,base_pixel;
    uint32_t bits;
    if(!(v->sprite_display_mask&bit) || x<s->x || x>=s->x+width)return false;
    dx=x-s->x;
    base_pixel=(v->regs[0x1d]&bit)?(dx>>1):dx;
    bits=((uint32_t)s->data[0]<<16)|((uint32_t)s->data[1]<<8)|s->data[2];
    if(v->regs[0x1c]&bit){
        unsigned pair=base_pixel>>1;
        uint8_t code=(uint8_t)((bits>>(22u-2u*pair))&3u);
        if(code==0u)return false;
        if(code==1u)*color=(uint8_t)(v->regs[0x25]&0x0fu);
        else if(code==2u)*color=(uint8_t)(v->regs[0x27u+n]&0x0fu);
        else *color=(uint8_t)(v->regs[0x26]&0x0fu);
        return true;
    }
    if(((bits>>(23u-base_pixel))&1u)==0u)return false;
    *color=(uint8_t)(v->regs[0x27u+n]&0x0fu);
    return true;
}

static uint8_t compose_sprites(bt_vicii6569 *v,unsigned x,bt_vicii_pixel_sample gfx,uint8_t base)
{
    unsigned n;
    uint8_t hitmask=0u,front_color=base,front_num=0xffu;
    for(n=0;n<8u;n++){
        uint8_t c;
        if(sprite_sample(v,n,x,&c)){
            hitmask|=(uint8_t)(1u<<n);
            if(front_num==0xffu){front_num=(uint8_t)n;front_color=c;}
        }
    }
    if(hitmask && (hitmask&(uint8_t)(hitmask-1u))){
        uint8_t before=v->regs[0x1e];
        v->regs[0x1e]|=hitmask;
        if(before==0u && v->regs[0x1e]!=0u)set_irq(v,0x04u);
    }
    if(hitmask && gfx.opaque){
        uint8_t before=v->regs[0x1f];
        v->regs[0x1f]|=hitmask;
        if(before==0u && v->regs[0x1f]!=0u)set_irq(v,0x02u);
    }
    if(front_num!=0xffu){
        uint8_t bit=(uint8_t)(1u<<front_num);
        if(!(gfx.opaque && (v->regs[0x1b]&bit)))return front_color;
    }
    return base;
}

static void render_cycle(bt_vicii6569 *v)
{
    /* Keep the VIC-II logical raster coordinate independent from the exported
       384x272 viewport.  Oracle comparison proves that this target viewport is
       four pixels to the right and one raster line above the original crop.
       Moving the logical X itself changes bitmap/sprite sampling and corrupts
       the picture; only the framebuffer destination is translated here. */
    int logical_first_x=((int)v->raster_cycle-17)*8+32;
    unsigned k;
    if(v->raster_line<17u || v->raster_line>288u)return;
    for(k=0;k<8u;k++){
        int logical_x=logical_first_x+(int)k;
        int output_x=logical_x+4;
        unsigned y=(unsigned)v->raster_line-17u;
        uint8_t pixel;
        unsigned hstart=v->open_left_border?0u:v->display_xstart;
        unsigned hstop=v->open_right_border?BT_VICII_NORMAL_WIDTH:v->display_xstop;
        if(output_x<0 || output_x>=(int)BT_VICII_NORMAL_WIDTH)continue;
        if(v->blank_this_line || v->border_vertical || logical_x<0 ||
           logical_x>=(int)BT_VICII_NORMAL_WIDTH || (unsigned)logical_x<hstart ||
           (unsigned)logical_x>=hstop){
            pixel=(uint8_t)(v->regs[0x20]&0x0fu);
        }else{
            bt_vicii_pixel_sample gfx=graphics_sample(v,(unsigned)logical_x);
            pixel=compose_sprites(v,(unsigned)logical_x,gfx,gfx.color);
        }
        v->framebuffer[y*BT_VICII_NORMAL_WIDTH+(unsigned)output_x]=pixel;
    }
}

static void update_bus_ownership(bt_vicii6569 *v)
{
    unsigned i;
    bool steal=read_steal_class_active(v,v->machine_cycle);
    bool ba=false;
    if(v->badline){
        uint64_t first=v->line_start_cycle+MATRIX_FETCH_CYCLE;
        uint64_t last=first+MATRIX_STEAL_CYCLES-1u;
        if(v->machine_cycle+3u>=first && v->machine_cycle<=last)ba=true;
    }
    for(i=0;i<v->sprite_span_count;i++){
        const bt_vicii_dma_span *s=&v->sprite_spans[i];
        if(v->machine_cycle+3u>=s->first_cycle && v->machine_cycle<=s->last_cycle)ba=true;
    }
    v->cpu_read_stolen=(uint8_t)steal;
    v->ba_low=(uint8_t)ba;
}

void bt_vicii6569_init(bt_vicii6569 *v,c64_bus *bus)
{
    if(!v)return;
    memset(v,0,sizeof(*v));
    v->bus=bus;
    v->border_vertical=1u;
    v->border_main=1u;
    v->display_ystart=0x37u;v->display_ystop=0xf7u;
    v->mem_counter_inc=TEXT_COLS;
    v->display_xstart=32u;v->display_xstop=352u;v->next_display_xstart=32u;v->next_display_xstop=352u;v->xsmooth=0u;
    memset(v->framebuffer,0,sizeof(v->framebuffer));
}

void bt_vicii6569_tick(bt_vicii6569 *v,uint64_t machine_cycle)
{
    if(!v)return;
    v->machine_cycle=machine_cycle;
    v->frame_number=machine_cycle/BT_VICII_PAL_CYCLES_PER_FRAME;
    v->raster_line=(uint16_t)((machine_cycle/BT_VICII_PAL_CYCLES_PER_LINE)%BT_VICII_PAL_LINES_PER_FRAME);
    v->raster_cycle=(uint8_t)(machine_cycle%BT_VICII_PAL_CYCLES_PER_LINE);
    v->line_start_cycle=machine_cycle-v->raster_cycle;

    if(v->raster_cycle==0u){
        if(v->raster_line==0u && machine_cycle!=0u)v->completed_frames++;
        line_start(v);
    }
    if(v->raster_line==0u && v->raster_cycle==1u && v->raster_line0_irq_pending){
        set_irq(v,0x01u);v->raster_line0_irq_pending=0u;
    }
    if(v->raster_cycle==MATRIX_FETCH_CYCLE && v->badline && !v->memory_fetch_done)fetch_matrix(v);
    if(v->raster_cycle==SPRITE_FETCH_CYCLE){
        check_sprite_dma(v);
        fetch_sprite_data(v);
    }
    update_bus_ownership(v);
    render_cycle(v);
    if(v->bus)c64_bus_set_vic_phi1(v->bus,c64_bus_vic_read(v->bus,(uint16_t)((v->raster_cycle*8u)&0x3fffu)));
    if(v->raster_cycle==BT_VICII_PAL_CYCLES_PER_LINE-1u)line_finish(v);
}

uint8_t bt_vicii6569_peek(const bt_vicii6569 *v,uint8_t reg)
{
    uint8_t x;
    reg&=0x3fu;
    if(reg>=0x2fu)return 0xffu;
    if(reg==0x11u)return (uint8_t)((v->regs[reg]&0x7fu)|((v->raster_line&0x100u)?0x80u:0u));
    if(reg==0x12u)return (uint8_t)v->raster_line;
    if(reg==0x16u)return (uint8_t)(v->regs[reg]|0xc0u);
    if(reg==0x18u)return (uint8_t)(v->regs[reg]|0x01u);
    if(reg==0x19u){
        x=(uint8_t)(v->irq_flags&0x0fu);
        if(v->irq_line)x|=0x80u;
        return (uint8_t)(x|0x70u);
    }
    if(reg==0x1au)return (uint8_t)((v->irq_mask&0x0fu)|0xf0u);
    if(reg>=0x20u && reg<=0x2eu)return (uint8_t)((v->regs[reg]&0x0fu)|0xf0u);
    return v->regs[reg];
}

uint8_t bt_vicii6569_read(bt_vicii6569 *v,uint8_t reg)
{
    uint8_t x;
    reg&=0x3fu;
    x=bt_vicii6569_peek(v,reg);
    if(reg==0x1eu || reg==0x1fu)v->regs[reg]=0u;
    return x;
}

void bt_vicii6569_write_phase(bt_vicii6569 *v,uint8_t reg,uint8_t value,bt_vicii_write_phase phase)
{
    unsigned n;
    uint8_t bit;
    reg&=0x3fu;
    if(reg>=0x2fu)return;
    if(reg<=0x0fu){
        v->regs[reg]=value;
        n=reg>>1;
        if(reg&1u)v->sprites[n].y=value;
        else v->sprites[n].x=(uint16_t)((v->sprites[n].x&0x100u)|value);
        return;
    }
    switch(reg){
        case 0x10:
            v->regs[reg]=value;
            for(n=0,bit=1u;n<8u;n++,bit=(uint8_t)(bit<<1))v->sprites[n].x=(uint16_t)((v->sprites[n].x&0xffu)|((value&bit)?0x100u:0u));
            break;
        case 0x11:{
            uint8_t old=v->regs[reg];
            v->regs[reg]=value;
            {
                uint16_t old_line=v->raster_irq_line;
                v->raster_irq_line=(uint16_t)((v->raster_irq_line&0xffu)|((uint16_t)(value&0x80u)<<1));
                raster_compare_changed(v,old_line);
            }
            d011_badline_transition(v,old,value);
            d011_border_transition(v,old,value);
            break;
        }
        case 0x12:{
            uint16_t old_line=v->raster_irq_line;
            v->regs[reg]=value;
            v->raster_irq_line=(uint16_t)((v->raster_irq_line&0x100u)|value);
            raster_compare_changed(v,old_line);
            break;
        }
        case 0x13: case 0x14:
            break;
        case 0x15:
            d015_store(v,value);
            break;
        case 0x18: case 0x1b: case 0x1c: case 0x1d:
            v->regs[reg]=value;
            break;
        case 0x16:
            d016_store(v,value);
            break;
        case 0x17:
            d017_store(v,value);
            break;
        case 0x19:
            d019_store(v,value,phase);
            break;
        case 0x1a:
            v->regs[reg]=(uint8_t)(value&0x0fu);
            v->irq_mask=(uint8_t)(value&0x0fu);
            irq_recompute(v);
            break;
        case 0x1e: case 0x1f:
            break;
        default:
            v->regs[reg]=(reg>=0x20u && reg<=0x2eu)?(uint8_t)(value&0x0fu):value;
            break;
    }
}

bool bt_vicii6569_irq(const bt_vicii6569 *v){return v && v->irq_line;}
bool bt_vicii6569_cpu_read_stolen(const bt_vicii6569 *v){return v && v->cpu_read_stolen;}


const uint8_t *bt_vicii6569_framebuffer(const bt_vicii6569 *v)
{
    return v?v->framebuffer:0;
}
