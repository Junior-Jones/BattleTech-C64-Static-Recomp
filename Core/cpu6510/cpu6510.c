#include "cpu6510.h"
#include <string.h>

typedef enum addr_mode { AM_IMP,AM_ACC,AM_IMM,AM_ZP,AM_ZPX,AM_ZPY,AM_ABS,AM_ABX,AM_ABY,AM_INX,AM_INY,AM_IND,AM_REL } addr_mode;
typedef enum operation {
 OP_ADC,OP_AHX,OP_ALR,OP_ANC,OP_AND,OP_ARR,OP_ASL,OP_AXS,
 OP_BCC,OP_BCS,OP_BEQ,OP_BIT,OP_BMI,OP_BNE,OP_BPL,OP_BRK,OP_BVC,OP_BVS,
 OP_CLC,OP_CLD,OP_CLI,OP_CLV,OP_CMP,OP_CPX,OP_CPY,
 OP_DCP,OP_DEC,OP_DEX,OP_DEY,OP_EOR,OP_INC,OP_INX,OP_INY,OP_ISC,
 OP_JAM,OP_JMP,OP_JSR,OP_LAS,OP_LAX,OP_LDA,OP_LDX,OP_LDY,OP_LSR,
 OP_NOP,OP_ORA,OP_PHA,OP_PHP,OP_PLA,OP_PLP,OP_RLA,OP_ROL,OP_ROR,OP_RRA,
 OP_RTI,OP_RTS,OP_SAX,OP_SBC,OP_SEC,OP_SED,OP_SEI,OP_SHX,OP_SHY,OP_SLO,
 OP_SRE,OP_STA,OP_STX,OP_STY,OP_TAS,OP_TAX,OP_TAY,OP_TSX,OP_TXA,OP_TXS,OP_TYA,OP_XAA
} operation;
typedef struct opcode_desc { operation op; addr_mode mode; } opcode_desc;
static const opcode_desc opcode_table[256] = {
#include "opcode_table.inc"
};

static void begin(cpu6510 *c,uint16_t a,cpu6510_bus_kind k){ if(c->bus.begin_cycle)c->bus.begin_cycle(c->bus.ctx,c->cycles,a,k); }
static uint8_t rd(cpu6510 *c,uint16_t a,cpu6510_bus_kind k){ begin(c,a,k); uint8_t v=c->bus.read?c->bus.read(c->bus.ctx,a,k):0xff; c->cycles++; return v; }
static void wr(cpu6510 *c,uint16_t a,uint8_t v,cpu6510_bus_kind k){ begin(c,a,k); if(c->bus.write)c->bus.write(c->bus.ctx,a,v,k); c->cycles++; }
static uint8_t fetch(cpu6510 *c){ uint8_t v=rd(c,c->pc,CPU6510_BUS_READ); c->pc++; return v; }
static uint16_t fetch16(cpu6510 *c){ uint16_t lo=fetch(c),hi=fetch(c); return (uint16_t)(lo|(hi<<8)); }
static void setf(cpu6510 *c,uint8_t f,bool on){ if(on)c->p|=f;else c->p&=(uint8_t)~f; c->p|=CPU6510_P_U; }
static bool getf(const cpu6510 *c,uint8_t f){ return (c->p&f)!=0; }
static void set_nz(cpu6510 *c,uint8_t v){ setf(c,CPU6510_P_Z,v==0); setf(c,CPU6510_P_N,(v&0x80)!=0); }
static void push(cpu6510 *c,uint8_t v){ wr(c,(uint16_t)(0x100u|c->sp),v,CPU6510_BUS_WRITE); c->sp--; }
static uint8_t pull(cpu6510 *c){ c->sp++; return rd(c,(uint16_t)(0x100u|c->sp),CPU6510_BUS_READ); }

void cpu6510_init(cpu6510 *c,cpu6510_bus bus){ memset(c,0,sizeof(*c)); c->bus=bus; c->sp=0xfd; c->p=CPU6510_P_U|CPU6510_P_I; c->irq_sampled=true; c->xaa_magic=0xef; c->lxa_magic=0xee; }
void cpu6510_set_status(cpu6510 *c,uint8_t p){ c->p=(uint8_t)(p|CPU6510_P_U); c->irq_sampled=getf(c,CPU6510_P_I); }
void cpu6510_set_irq(cpu6510 *c,bool level){ c->irq_line=level; }
void cpu6510_set_nmi(cpu6510 *c,bool level){ if(level&&!c->nmi_line)c->nmi_edge_pending=true; c->nmi_line=level; }

uint32_t cpu6510_reset(cpu6510 *c){
 uint64_t s=c->cycles; c->jammed=false;
 rd(c,c->pc,CPU6510_BUS_DUMMY_READ); rd(c,c->pc,CPU6510_BUS_DUMMY_READ);
 for(int i=0;i<3;i++){ rd(c,(uint16_t)(0x100u|c->sp),CPU6510_BUS_DUMMY_READ); c->sp--; }
 uint16_t lo=rd(c,0xfffc,CPU6510_BUS_READ),hi=rd(c,0xfffd,CPU6510_BUS_READ); c->pc=(uint16_t)(lo|(hi<<8));
 setf(c,CPU6510_P_I,true); c->irq_sampled=true; c->nmi_edge_pending=false; return (uint32_t)(c->cycles-s);
}

static uint8_t operand_read(cpu6510 *c,addr_mode m,uint16_t *ea){
 uint16_t a=0,base,prov; uint8_t zp,lo,hi,v;
 switch(m){
  case AM_IMM: a=c->pc; v=fetch(c); break;
  case AM_ZP: zp=fetch(c); a=zp; v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_ZPX: zp=fetch(c); rd(c,zp,CPU6510_BUS_DUMMY_READ); a=(uint8_t)(zp+c->x); v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_ZPY: zp=fetch(c); rd(c,zp,CPU6510_BUS_DUMMY_READ); a=(uint8_t)(zp+c->y); v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_ABS: a=fetch16(c); v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_ABX:
   base=fetch16(c); a=(uint16_t)(base+c->x); prov=(uint16_t)((base&0xff00u)|(a&0xffu));
   v=rd(c,prov,prov==a?CPU6510_BUS_READ:CPU6510_BUS_DUMMY_READ); if(prov!=a)v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_ABY:
   base=fetch16(c); a=(uint16_t)(base+c->y); prov=(uint16_t)((base&0xff00u)|(a&0xffu));
   v=rd(c,prov,prov==a?CPU6510_BUS_READ:CPU6510_BUS_DUMMY_READ); if(prov!=a)v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_INX:
   zp=fetch(c); rd(c,zp,CPU6510_BUS_DUMMY_READ); lo=rd(c,(uint8_t)(zp+c->x),CPU6510_BUS_READ); hi=rd(c,(uint8_t)(zp+c->x+1),CPU6510_BUS_READ); a=(uint16_t)(lo|(hi<<8)); v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_INY:
   zp=fetch(c); lo=rd(c,zp,CPU6510_BUS_READ); hi=rd(c,(uint8_t)(zp+1),CPU6510_BUS_READ); base=(uint16_t)(lo|(hi<<8)); a=(uint16_t)(base+c->y); prov=(uint16_t)((base&0xff00u)|(a&0xffu));
   v=rd(c,prov,prov==a?CPU6510_BUS_READ:CPU6510_BUS_DUMMY_READ); if(prov!=a)v=rd(c,a,CPU6510_BUS_READ); break;
  default: v=0; break;
 }
 if(ea) *ea=a;
 return v;
}

typedef struct addr_info { uint16_t base,addr,prov; bool crossed; } addr_info;
static addr_info write_address(cpu6510 *c,addr_mode m){
 addr_info z={0,0,0,false}; uint8_t zp,lo,hi;
 switch(m){
  case AM_ZP: zp=fetch(c); z.base=z.addr=zp; break;
  case AM_ZPX: zp=fetch(c); rd(c,zp,CPU6510_BUS_DUMMY_READ); z.base=zp; z.addr=(uint8_t)(zp+c->x); break;
  case AM_ZPY: zp=fetch(c); rd(c,zp,CPU6510_BUS_DUMMY_READ); z.base=zp; z.addr=(uint8_t)(zp+c->y); break;
  case AM_ABS: z.base=z.addr=fetch16(c); break;
  case AM_ABX: z.base=fetch16(c); z.addr=(uint16_t)(z.base+c->x); z.prov=(uint16_t)((z.base&0xff00u)|(z.addr&0xffu)); z.crossed=z.prov!=z.addr; rd(c,z.prov,CPU6510_BUS_DUMMY_READ); break;
  case AM_ABY: z.base=fetch16(c); z.addr=(uint16_t)(z.base+c->y); z.prov=(uint16_t)((z.base&0xff00u)|(z.addr&0xffu)); z.crossed=z.prov!=z.addr; rd(c,z.prov,CPU6510_BUS_DUMMY_READ); break;
  case AM_INX:
   zp=fetch(c); rd(c,zp,CPU6510_BUS_DUMMY_READ); lo=rd(c,(uint8_t)(zp+c->x),CPU6510_BUS_READ); hi=rd(c,(uint8_t)(zp+c->x+1),CPU6510_BUS_READ); z.base=z.addr=(uint16_t)(lo|(hi<<8)); break;
  case AM_INY:
   zp=fetch(c); lo=rd(c,zp,CPU6510_BUS_READ); hi=rd(c,(uint8_t)(zp+1),CPU6510_BUS_READ); z.base=(uint16_t)(lo|(hi<<8)); z.addr=(uint16_t)(z.base+c->y); z.prov=(uint16_t)((z.base&0xff00u)|(z.addr&0xffu)); z.crossed=z.prov!=z.addr; rd(c,z.prov,CPU6510_BUS_DUMMY_READ); break;
  default: break;
 }
 return z;
}

static uint8_t rmw_read(cpu6510 *c,addr_mode m,uint16_t *ea){
 addr_info z; uint8_t zp,lo,hi; uint16_t a=0; uint8_t v=0;
 switch(m){
  case AM_ZP: zp=fetch(c); a=zp; v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_ZPX: zp=fetch(c); rd(c,zp,CPU6510_BUS_DUMMY_READ); a=(uint8_t)(zp+c->x); v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_ABS: a=fetch16(c); v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_ABX: case AM_ABY:
   z.base=fetch16(c); z.addr=(uint16_t)(z.base+(m==AM_ABX?c->x:c->y)); z.prov=(uint16_t)((z.base&0xff00u)|(z.addr&0xffu)); rd(c,z.prov,CPU6510_BUS_DUMMY_READ); a=z.addr; v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_INX:
   zp=fetch(c); rd(c,zp,CPU6510_BUS_DUMMY_READ); lo=rd(c,(uint8_t)(zp+c->x),CPU6510_BUS_READ); hi=rd(c,(uint8_t)(zp+c->x+1),CPU6510_BUS_READ); a=(uint16_t)(lo|(hi<<8)); v=rd(c,a,CPU6510_BUS_READ); break;
  case AM_INY:
   zp=fetch(c); lo=rd(c,zp,CPU6510_BUS_READ); hi=rd(c,(uint8_t)(zp+1),CPU6510_BUS_READ); z.base=(uint16_t)(lo|(hi<<8)); z.addr=(uint16_t)(z.base+c->y); z.prov=(uint16_t)((z.base&0xff00u)|(z.addr&0xffu)); rd(c,z.prov,CPU6510_BUS_DUMMY_READ); a=z.addr; v=rd(c,a,CPU6510_BUS_READ); break;
  default: break;
 }
 if(ea) *ea=a;
 return v;
}

static void adc(cpu6510 *c,uint8_t v){
 uint8_t a=c->a; unsigned carry=getf(c,CPU6510_P_C)?1u:0u;
 if(getf(c,CPU6510_P_D)){
  unsigned t=(a&15u)+(v&15u)+carry; if(t>9)t+=6;
  if(t<=15)t=(t&15u)+(a&0xf0u)+(v&0xf0u); else t=(t&15u)+(a&0xf0u)+(v&0xf0u)+0x10u;
  setf(c,CPU6510_P_Z,((((unsigned)a+v+carry)&0xffu)==0)); setf(c,CPU6510_P_N,(t&0x80u)!=0);
  setf(c,CPU6510_P_V,((a^t)&0x80u)!=0 && ((a^v)&0x80u)==0); if((t&0x1f0u)>0x90u)t+=0x60u; setf(c,CPU6510_P_C,(t&0xff0u)>0xf0u); c->a=(uint8_t)t;
 } else {
  unsigned t=(unsigned)a+v+carry; uint8_t r=(uint8_t)t; set_nz(c,r); setf(c,CPU6510_P_V,((a^v)&0x80u)==0 && ((a^r)&0x80u)!=0); setf(c,CPU6510_P_C,t>0xffu); c->a=r;
 }
}
static void sbc(cpu6510 *c,uint8_t v){
 uint8_t a=c->a; unsigned borrow=getf(c,CPU6510_P_C)?0u:1u; uint16_t t=(uint16_t)((unsigned)a-(unsigned)v-borrow); uint8_t r=(uint8_t)t;
 setf(c,CPU6510_P_C,t<0x100u); set_nz(c,r); setf(c,CPU6510_P_V,((a^r)&0x80u)!=0 && ((a^v)&0x80u)!=0);
 if(getf(c,CPU6510_P_D)){
  unsigned ta=(a&15u)-(v&15u)-borrow; if(ta&0x10u) ta=((ta-6u)&15u)+((a&0xf0u)-(v&0xf0u)-0x10u); else ta=(ta&15u)+((a&0xf0u)-(v&0xf0u)); if(ta&0x100u)ta-=0x60u; c->a=(uint8_t)ta;
 } else c->a=r;
}
static void cmpv(cpu6510 *c,uint8_t a,uint8_t b){ unsigned t=(unsigned)a-(unsigned)b; setf(c,CPU6510_P_C,a>=b); set_nz(c,(uint8_t)t); }

static uint8_t rmw_apply(cpu6510 *c,operation op,uint8_t old){
 uint8_t n=old,ci=(uint8_t)(getf(c,CPU6510_P_C)?1:0);
 switch(op){
  case OP_ASL: case OP_SLO: setf(c,CPU6510_P_C,(old&0x80)!=0); n=(uint8_t)(old<<1); set_nz(c,n); break;
  case OP_LSR: case OP_SRE: setf(c,CPU6510_P_C,(old&1)!=0); n=(uint8_t)(old>>1); set_nz(c,n); break;
  case OP_ROL: case OP_RLA: setf(c,CPU6510_P_C,(old&0x80)!=0); n=(uint8_t)((old<<1)|ci); set_nz(c,n); break;
  case OP_ROR: case OP_RRA: setf(c,CPU6510_P_C,(old&1)!=0); n=(uint8_t)((old>>1)|(ci<<7)); set_nz(c,n); break;
  case OP_INC: case OP_ISC: n=(uint8_t)(old+1); set_nz(c,n); break;
  case OP_DEC: case OP_DCP: n=(uint8_t)(old-1); set_nz(c,n); break;
  default: break;
 }
 return n;
}

static bool is_rmw(operation o){ return o==OP_ASL||o==OP_LSR||o==OP_ROL||o==OP_ROR||o==OP_INC||o==OP_DEC||o==OP_SLO||o==OP_RLA||o==OP_SRE||o==OP_RRA||o==OP_DCP||o==OP_ISC; }
static bool is_store(operation o){ return o==OP_STA||o==OP_STX||o==OP_STY||o==OP_SAX||o==OP_AHX||o==OP_TAS||o==OP_SHX||o==OP_SHY; }
static bool is_branch(operation o){ return o>=OP_BCC&&o<=OP_BVS && o!=OP_BIT && o!=OP_BRK; }

static bool branch_condition(const cpu6510 *c,operation o){
 switch(o){ case OP_BCC:return !getf(c,CPU6510_P_C); case OP_BCS:return getf(c,CPU6510_P_C); case OP_BEQ:return getf(c,CPU6510_P_Z); case OP_BMI:return getf(c,CPU6510_P_N); case OP_BNE:return !getf(c,CPU6510_P_Z); case OP_BPL:return !getf(c,CPU6510_P_N); case OP_BVC:return !getf(c,CPU6510_P_V); case OP_BVS:return getf(c,CPU6510_P_V); default:return false; }
}
static void branch(cpu6510 *c,bool take){ int8_t d=(int8_t)fetch(c); if(!take)return; uint16_t old=c->pc,target=(uint16_t)(old+d); rd(c,old,CPU6510_BUS_DUMMY_READ); if((old&0xff00u)!=(target&0xff00u))rd(c,(uint16_t)((old&0xff00u)|(target&0xffu)),CPU6510_BUS_DUMMY_READ); c->pc=target; }

static void interrupt_sequence(cpu6510 *c,bool nmi){
 rd(c,c->pc,CPU6510_BUS_DUMMY_READ); rd(c,c->pc,CPU6510_BUS_DUMMY_READ); push(c,(uint8_t)(c->pc>>8)); push(c,(uint8_t)c->pc); push(c,(uint8_t)((c->p|CPU6510_P_U)&~CPU6510_P_B)); setf(c,CPU6510_P_I,true); uint16_t vec=nmi?0xfffa:0xfffe; uint16_t lo=rd(c,vec,CPU6510_BUS_READ),hi=rd(c,(uint16_t)(vec+1),CPU6510_BUS_READ); c->pc=(uint16_t)(lo|(hi<<8)); c->irq_sampled=true;
}

static void apply_read(cpu6510 *c,operation o,addr_mode m,uint8_t v){
 uint8_t t; unsigned u;
 switch(o){
  case OP_ADC: adc(c,v); break; case OP_SBC:sbc(c,v); break;
  case OP_AND:c->a&=v;set_nz(c,c->a);break; case OP_ORA:c->a|=v;set_nz(c,c->a);break; case OP_EOR:c->a^=v;set_nz(c,c->a);break;
  case OP_LDA:c->a=v;set_nz(c,v);break; case OP_LDX:c->x=v;set_nz(c,v);break; case OP_LDY:c->y=v;set_nz(c,v);break;
  case OP_LAX: t=(m==AM_IMM)?(uint8_t)((c->a|c->lxa_magic)&v):v; c->a=c->x=t; set_nz(c,t); break;
  case OP_LAS:t=(uint8_t)(v&c->sp);c->a=c->x=c->sp=t;set_nz(c,t);break;
  case OP_CMP:cmpv(c,c->a,v);break; case OP_CPX:cmpv(c,c->x,v);break; case OP_CPY:cmpv(c,c->y,v);break;
  case OP_BIT:setf(c,CPU6510_P_Z,(c->a&v)==0);setf(c,CPU6510_P_N,(v&0x80)!=0);setf(c,CPU6510_P_V,(v&0x40)!=0);break;
  case OP_ANC:c->a&=v;set_nz(c,c->a);setf(c,CPU6510_P_C,getf(c,CPU6510_P_N));break;
  case OP_ALR:c->a&=v;setf(c,CPU6510_P_C,(c->a&1)!=0);c->a>>=1;set_nz(c,c->a);break;
  case OP_ARR:
   t=(uint8_t)(c->a&v); if(getf(c,CPU6510_P_D)){ unsigned t2=((unsigned)t)|((getf(c,CPU6510_P_C)?1u:0u)<<8); t2>>=1; setf(c,CPU6510_P_N,getf(c,CPU6510_P_C)); setf(c,CPU6510_P_Z,(uint8_t)t2==0); setf(c,CPU6510_P_V,((t2^t)&0x40u)!=0); if(((t&15u)+(t&1u))>5u)t2=(t2&0xf0u)|((t2+6u)&15u); if(((t&0xf0u)+(t&0x10u))>0x50u){t2=(t2&15u)|((t2+0x60u)&0xf0u);setf(c,CPU6510_P_C,true);}else setf(c,CPU6510_P_C,false); c->a=(uint8_t)t2; }
   else { unsigned t2=((unsigned)t)|((getf(c,CPU6510_P_C)?1u:0u)<<8); t2>>=1;c->a=(uint8_t)t2;set_nz(c,c->a);setf(c,CPU6510_P_C,(c->a&0x40)!=0);setf(c,CPU6510_P_V,((c->a&0x40u)^((c->a&0x20u)<<1))!=0); } break;
  case OP_AXS:u=(unsigned)(c->a&c->x)-(unsigned)v;setf(c,CPU6510_P_C,u<0x100u);c->x=(uint8_t)u;set_nz(c,c->x);break;
  case OP_XAA:c->a=(uint8_t)((c->a|c->xaa_magic)&c->x&v);set_nz(c,c->a);break;
  case OP_NOP: default:break;
 }
}

static cpu6510_step_result execute_selected_opcode(cpu6510 *c,cpu6510_step_result r,uint64_t start,uint8_t selected_opcode){
 opcode_desc d=opcode_table[selected_opcode]; r.opcode=selected_opcode;
 if(d.op==OP_JAM){
  /* NMOS KIL/JAM performs two further reads from the post-opcode address,
     then locks the address bus there while the architectural PC remains
     on the JAM opcode until reset. */
  rd(c,c->pc,CPU6510_BUS_DUMMY_READ);
  rd(c,c->pc,CPU6510_BUS_DUMMY_READ);
  c->pc=r.opcode_pc;
  c->jammed=true;
  r.stop_reason=CPU6510_STOP_JAM;
  r.cycles=(uint32_t)(c->cycles-start);
  return r;
 }
 if(is_branch(d.op)){ branch(c,branch_condition(c,d.op)); r.cycles=(uint32_t)(c->cycles-start); return r; }
 switch(d.op){
  case OP_BRK:{ rd(c,c->pc,CPU6510_BUS_READ); c->pc++; push(c,(uint8_t)(c->pc>>8)); push(c,(uint8_t)c->pc); push(c,(uint8_t)(c->p|CPU6510_P_B|CPU6510_P_U)); setf(c,CPU6510_P_I,true); uint16_t lo=rd(c,0xfffe,CPU6510_BUS_READ),hi=rd(c,0xffff,CPU6510_BUS_READ); c->pc=(uint16_t)(lo|(hi<<8)); break; }
  case OP_JSR:{ uint8_t lo=fetch(c); rd(c,(uint16_t)(0x100u|c->sp),CPU6510_BUS_DUMMY_READ); push(c,(uint8_t)(c->pc>>8)); push(c,(uint8_t)c->pc); uint8_t hi=fetch(c); c->pc=(uint16_t)(lo|(hi<<8)); break; }
  case OP_RTS:{ rd(c,c->pc,CPU6510_BUS_DUMMY_READ); rd(c,(uint16_t)(0x100u|c->sp),CPU6510_BUS_DUMMY_READ); uint8_t lo=pull(c),hi=pull(c); uint16_t a=(uint16_t)(lo|(hi<<8)); rd(c,a,CPU6510_BUS_DUMMY_READ); c->pc=(uint16_t)(a+1); break; }
  case OP_RTI:{ rd(c,c->pc,CPU6510_BUS_DUMMY_READ); rd(c,(uint16_t)(0x100u|c->sp),CPU6510_BUS_DUMMY_READ); c->p=(uint8_t)(pull(c)|CPU6510_P_U); uint8_t lo=pull(c),hi=pull(c); c->pc=(uint16_t)(lo|(hi<<8)); break; }
  case OP_JMP: if(d.mode==AM_ABS)c->pc=fetch16(c); else { uint16_t p=fetch16(c); uint8_t lo=rd(c,p,CPU6510_BUS_READ); uint16_t q=(uint16_t)((p&0xff00u)|((p+1)&0xffu)); uint8_t hi=rd(c,q,CPU6510_BUS_READ); c->pc=(uint16_t)(lo|(hi<<8)); } break;
  case OP_PHA: rd(c,c->pc,CPU6510_BUS_DUMMY_READ);push(c,c->a);break;
  case OP_PHP: rd(c,c->pc,CPU6510_BUS_DUMMY_READ);push(c,(uint8_t)(c->p|CPU6510_P_B|CPU6510_P_U));break;
  case OP_PLA: rd(c,c->pc,CPU6510_BUS_DUMMY_READ);rd(c,(uint16_t)(0x100u|c->sp),CPU6510_BUS_DUMMY_READ);c->a=pull(c);set_nz(c,c->a);break;
  case OP_PLP: rd(c,c->pc,CPU6510_BUS_DUMMY_READ);rd(c,(uint16_t)(0x100u|c->sp),CPU6510_BUS_DUMMY_READ);c->p=(uint8_t)(pull(c)|CPU6510_P_U);break;
  default:
   if(is_store(d.op)){
    addr_info z=write_address(c,d.mode); uint8_t v=0; uint16_t wa=z.addr;
    switch(d.op){ case OP_STA:v=c->a;break;case OP_STX:v=c->x;break;case OP_STY:v=c->y;break;case OP_SAX:v=(uint8_t)(c->a&c->x);break;
     case OP_AHX:v=(uint8_t)(c->a&c->x&(uint8_t)((z.base>>8)+1));break;
     case OP_TAS:c->sp=(uint8_t)(c->a&c->x);v=(uint8_t)(c->sp&(uint8_t)((z.base>>8)+1));break;
     case OP_SHX:v=(uint8_t)(c->x&(uint8_t)((z.base>>8)+1));break;case OP_SHY:v=(uint8_t)(c->y&(uint8_t)((z.base>>8)+1));break;default:break; }
    if((d.op==OP_AHX||d.op==OP_TAS||d.op==OP_SHX||d.op==OP_SHY)&&z.crossed) wa=(uint16_t)(((uint16_t)v<<8)|(z.addr&0xffu));
    wr(c,wa,v,CPU6510_BUS_WRITE);
   } else if(is_rmw(d.op) && d.mode!=AM_ACC){
    uint16_t a; uint8_t old=rmw_read(c,d.mode,&a); wr(c,a,old,CPU6510_BUS_DUMMY_WRITE); uint8_t n=rmw_apply(c,d.op,old); wr(c,a,n,CPU6510_BUS_WRITE);
    if(d.op==OP_SLO){c->a|=n;set_nz(c,c->a);} else if(d.op==OP_RLA){c->a&=n;set_nz(c,c->a);} else if(d.op==OP_SRE){c->a^=n;set_nz(c,c->a);} else if(d.op==OP_RRA)adc(c,n); else if(d.op==OP_DCP)cmpv(c,c->a,n); else if(d.op==OP_ISC)sbc(c,n);
   } else if(d.mode==AM_ACC){
    rd(c,c->pc,CPU6510_BUS_DUMMY_READ); uint8_t old=c->a,n=rmw_apply(c,d.op,old); c->a=n;
   } else if(d.mode==AM_IMP){
    rd(c,c->pc,CPU6510_BUS_DUMMY_READ);
    switch(d.op){ case OP_CLC:setf(c,CPU6510_P_C,false);break;case OP_SEC:setf(c,CPU6510_P_C,true);break;case OP_CLD:setf(c,CPU6510_P_D,false);break;case OP_SED:setf(c,CPU6510_P_D,true);break;case OP_CLI:setf(c,CPU6510_P_I,false);break;case OP_SEI:setf(c,CPU6510_P_I,true);break;case OP_CLV:setf(c,CPU6510_P_V,false);break;
     case OP_DEX:c->x--;set_nz(c,c->x);break;case OP_DEY:c->y--;set_nz(c,c->y);break;case OP_INX:c->x++;set_nz(c,c->x);break;case OP_INY:c->y++;set_nz(c,c->y);break;
     case OP_TAX:c->x=c->a;set_nz(c,c->x);break;case OP_TAY:c->y=c->a;set_nz(c,c->y);break;case OP_TSX:c->x=c->sp;set_nz(c,c->x);break;case OP_TXA:c->a=c->x;set_nz(c,c->a);break;case OP_TXS:c->sp=c->x;break;case OP_TYA:c->a=c->y;set_nz(c,c->a);break;case OP_NOP:default:break; }
   } else { uint8_t v=operand_read(c,d.mode,0); apply_read(c,d.op,d.mode,v); }
   break;
 }
 r.cycles=(uint32_t)(c->cycles-start); r.stop_reason=c->jammed?CPU6510_STOP_JAM:CPU6510_STOP_NONE; return r;
}

cpu6510_step_result cpu6510_service_pending_interrupt(cpu6510 *c){
 cpu6510_step_result r; uint64_t start;
 memset(&r,0,sizeof(r));
 if(!c){ r.stop_reason=CPU6510_STOP_INTERRUPT_NOT_PENDING; return r; }
 start=c->cycles;
 if(c->jammed){ r.stop_reason=CPU6510_STOP_JAM; return r; }
 if(c->nmi_edge_pending){ c->nmi_edge_pending=false; interrupt_sequence(c,true); r.serviced_nmi=true; r.cycles=(uint32_t)(c->cycles-start); return r; }
 if(c->irq_line&&!c->irq_sampled){ interrupt_sequence(c,false); r.serviced_irq=true; r.cycles=(uint32_t)(c->cycles-start); return r; }
 r.stop_reason=CPU6510_STOP_INTERRUPT_NOT_PENDING;
 return r;
}

cpu6510_step_result cpu6510_step_fixed(cpu6510 *c,uint16_t expected_pc,uint8_t expected_opcode){
 cpu6510_step_result r; memset(&r,0,sizeof(r)); uint64_t start=c->cycles;
 r.opcode_pc=c->pc; r.opcode=expected_opcode;
 if(c->jammed){ r.stop_reason=CPU6510_STOP_JAM; return r; }
 if(c->nmi_edge_pending || (c->irq_line&&!c->irq_sampled)){
  r.stop_reason=CPU6510_STOP_AOT_INTERRUPT_PENDING;
  return r;
 }
 if(c->pc!=expected_pc){ r.stop_reason=CPU6510_STOP_AOT_PC_MISMATCH; return r; }
 c->irq_sampled=getf(c,CPU6510_P_I);
 {
  uint8_t actual=rd(c,c->pc,CPU6510_BUS_OPCODE_READ);
  if(actual!=expected_opcode){
   r.stop_reason=CPU6510_STOP_AOT_SOURCE_MISMATCH;
   r.cycles=(uint32_t)(c->cycles-start);
   return r;
  }
 }
 c->pc++;
 return execute_selected_opcode(c,r,start,expected_opcode);
}
