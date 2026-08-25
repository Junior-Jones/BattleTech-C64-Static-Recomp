#include "c64_input.h"
#include <string.h>
void c64_input_init(c64_input *in){memset(in,0,sizeof(*in));}
bool c64_input_set_key(c64_input *in,uint8_t row,uint8_t column,bool pressed){uint64_t bit;if(!in||row>7u||column>7u)return false;bit=UINT64_C(1)<<(row*8u+column);if(pressed)in->keyboard|=bit;else in->keyboard&=~bit;return true;}
void c64_input_set_joystick(c64_input *in,unsigned port,uint8_t controls){if(!in)return;controls&=0x1fu;if(port==1u)in->joystick1=controls;else if(port==2u)in->joystick2=controls;}
void c64_input_set_restore(c64_input *in,bool pressed){if(in)in->restore=pressed;}
uint8_t c64_input_cia1_pins(const c64_input *in,unsigned port,uint8_t pra,uint8_t ddra,uint8_t prb,uint8_t ddrb){uint8_t lowa,lowb;unsigned pass,row,col;if(!in)return 0xffu;lowa=(uint8_t)((~pra)&ddra);lowb=(uint8_t)((~prb)&ddrb);lowa|=in->joystick2;lowb|=in->joystick1;for(pass=0;pass<16u;pass++){uint8_t oa=lowa,ob=lowb;for(row=0;row<8u;row++)for(col=0;col<8u;col++)if(in->keyboard&(UINT64_C(1)<<(row*8u+col))){uint8_t rb=(uint8_t)(1u<<row),ca=(uint8_t)(1u<<col);if(lowb&rb)lowa|=ca;if(lowa&ca)lowb|=rb;}if(oa==lowa&&ob==lowb)break;}return port==0u?(uint8_t)~lowa:(uint8_t)~lowb;}
