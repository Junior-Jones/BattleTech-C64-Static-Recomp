#include "battletech_frontend_input.h"
#include <stdio.h>
#include <string.h>

typedef struct event_rec {bt_static_input_event e;} event_rec;
static event_rec events[32]; static unsigned event_count; static bt_static_status fake_status;

int bt_static_core_get_status(const bt_static_core *core, bt_static_status *status){(void)core; if(!status)return BT_STATIC_ERR_BAD_ARGUMENT;*status=fake_status;return BT_STATIC_OK;}
int bt_static_core_submit_input(bt_static_core *core,const bt_static_input_event *event){(void)core;if(!event||event_count>=32)return BT_STATIC_ERR_INTERNAL;events[event_count++].e=*event;return BT_STATIC_OK;}

static int ck(int cond,const char *msg){if(!cond){fprintf(stderr,"FAIL %s\n",msg);return 0;}printf("PASS %s\n",msg);return 1;}
int main(void){bt_frontend_input_state s;bt_static_core *fake=(bt_static_core*)1;int ok=1;memset(&fake_status,0,sizeof(fake_status));fake_status.cycle=1234;bt_frontend_input_init(&s);
 ok&=ck(bt_frontend_input_submit(fake,&s,BT_FRONTEND_KEY_UP,1)==BT_STATIC_OK,"up press accepted");
 ok&=ck(events[0].e.kind==BT_STATIC_INPUT_JOYSTICK2&&events[0].e.value==BT_STATIC_JOYSTICK_UP,"up maps to joystick port 2");
 ok&=ck(bt_frontend_input_submit(fake,&s,BT_FRONTEND_KEY_RIGHT,1)==BT_STATIC_OK,"right press accepted");
 ok&=ck(events[1].e.value==(BT_STATIC_JOYSTICK_UP|BT_STATIC_JOYSTICK_RIGHT),"direction mask preserves simultaneous input");
 ok&=ck(bt_frontend_input_submit(fake,&s,BT_FRONTEND_KEY_FIRE,1)==BT_STATIC_OK,"fire press accepted");
 ok&=ck(events[2].e.value==(BT_STATIC_JOYSTICK_UP|BT_STATIC_JOYSTICK_RIGHT|BT_STATIC_JOYSTICK_FIRE),"fire joins joystick mask");
 ok&=ck(bt_frontend_input_submit(fake,&s,BT_FRONTEND_KEY_UP,0)==BT_STATIC_OK,"up release accepted");
 ok&=ck(events[3].e.value==(BT_STATIC_JOYSTICK_RIGHT|BT_STATIC_JOYSTICK_FIRE),"up release preserves other joystick signals");
 ok&=ck(bt_frontend_input_submit(fake,&s,BT_FRONTEND_KEY_MENU_SPACE,1)==BT_STATIC_OK,"space press accepted");
 ok&=ck(events[4].e.kind==BT_STATIC_INPUT_KEY_MATRIX&&events[4].e.control==BT_STATIC_KEY_SPACE&&events[4].e.value==1,"space maps to real C64 matrix key");
 ok&=ck(bt_frontend_input_submit(fake,&s,BT_FRONTEND_KEY_C64_RETURN,1)==BT_STATIC_OK,"return press accepted");
 ok&=ck(events[5].e.control==BT_STATIC_KEY_RETURN,"return maps to real C64 matrix key");
 {unsigned before=event_count;ok&=ck(bt_frontend_input_submit(fake,&s,BT_FRONTEND_KEY_C64_RETURN,1)==BT_STATIC_OK&&event_count==before,"repeat keydown is de-duplicated");}
 ok&=ck(bt_frontend_input_submit(fake,&s,BT_FRONTEND_KEY_C64_Y,1)==BT_STATIC_OK,"Y press accepted");
 ok&=ck(events[6].e.kind==BT_STATIC_INPUT_KEY_MATRIX&&events[6].e.control==BT_STATIC_KEY_Y,"Y maps to real C64 matrix key");
 ok&=ck(bt_frontend_input_submit(fake,&s,BT_FRONTEND_KEY_C64_N,1)==BT_STATIC_OK,"N press accepted");
 ok&=ck(events[7].e.kind==BT_STATIC_INPUT_KEY_MATRIX&&events[7].e.control==BT_STATIC_KEY_N,"N maps to real C64 matrix key");
 ok&=ck(events[0].e.cycle==1234&&events[7].e.cycle==1234,"adapter cycle-stamps through opaque core status");
 {unsigned before=event_count;ok&=ck(bt_frontend_input_reset(fake,&s)==BT_STATIC_OK,"frontend input reset accepted");
  ok&=ck(event_count==before+7u,"frontend reset releases joystick and all owned matrix keys");
  ok&=ck(events[before].e.kind==BT_STATIC_INPUT_JOYSTICK2&&events[before].e.value==0u,"frontend reset clears joystick port 2");
  ok&=ck(events[before+1u].e.kind==BT_STATIC_INPUT_KEY_MATRIX&&events[before+1u].e.control==BT_STATIC_KEY_SPACE&&events[before+1u].e.value==0u,"frontend reset releases C64 Space");
  ok&=ck(s.joystick2_mask==0u&&s.key_down[BT_FRONTEND_KEY_C64_N]==0u,"frontend reset clears de-duplication state");}
 printf("FRONTEND_INPUT_ADAPTER_TEST %s events=%u\n",ok?"PASS":"FAIL",event_count);return ok?0:1;}
