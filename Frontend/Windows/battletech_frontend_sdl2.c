#define _POSIX_C_SOURCE 200809L
#define SDL_MAIN_HANDLED
#include <SDL.h>
#ifdef _WIN32
#include <SDL_syswm.h>
#endif
#include "battletech_frontend_input.h"
#include "battletech_frontend_ipc.h"
#include "battletech_game_entry.h"
#include "bt_static_core.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define BT_MKDIR(path) _mkdir(path)
#define BT_SEP '\\'
#else
#include <unistd.h>
#define BT_MKDIR(path) mkdir((path),0700)
#define BT_SEP '/'
#endif

#define BT_GAME_TITLE "BattleTech: The Crescent Hawk's Inception (C64)"
#ifdef _WIN32
#define BT_WM_GAME_RETURN (WM_APP+0x314)
#endif

typedef struct file_buf { uint8_t *p; size_t n; } file_buf;
typedef struct options {
 const char *basic,*kernal,*chargen,*drive,*disk1,*disk2,*save_dir,*screenshot_dir,*input_script;
 uint64_t frame_limit,screenshot_every,space_at_frame,space_every,side_at_frame; unsigned long long launcher_hwnd;
 int fullscreen,mute,print_save_path,correct_aspect,vsync,scale,pause_focus_loss,turbo,headless,side_value;
 int volume,audio_latency,input_source,deadzone,key_up,key_down,key_left,key_right,key_fire,key_space,key_return;
} options;
typedef struct media_cache { uint8_t *bytes; size_t size; } media_cache;
typedef struct scripted_input { uint64_t frame; bt_frontend_key key; int down; } scripted_input;

static int scripted_key(const char *name,bt_frontend_key *key){if(!strcmp(name,"up"))*key=BT_FRONTEND_KEY_UP;else if(!strcmp(name,"down"))*key=BT_FRONTEND_KEY_DOWN;else if(!strcmp(name,"left"))*key=BT_FRONTEND_KEY_LEFT;else if(!strcmp(name,"right"))*key=BT_FRONTEND_KEY_RIGHT;else if(!strcmp(name,"fire"))*key=BT_FRONTEND_KEY_FIRE;else if(!strcmp(name,"space"))*key=BT_FRONTEND_KEY_MENU_SPACE;else if(!strcmp(name,"return"))*key=BT_FRONTEND_KEY_C64_RETURN;else if(!strcmp(name,"y"))*key=BT_FRONTEND_KEY_C64_Y;else if(!strcmp(name,"n"))*key=BT_FRONTEND_KEY_C64_N;else return 0;return 1;}
static int compare_scripted_input(const void *a,const void *b){const scripted_input *x=(const scripted_input*)a,*y=(const scripted_input*)b;if(x->frame<y->frame)return -1;if(x->frame>y->frame)return 1;/* Release before a same-frame press so adjacent ranges remain held. */return x->down==y->down?0:(x->down?1:-1);}
static int load_input_script(const char *path,scripted_input **out,size_t *count){FILE*f=NULL;scripted_input *items=NULL;size_t used=0,cap=0;unsigned long long frame,duration;char name[32],line[160];int ok=0;if(!path)return 1;if(!out||!count)return 0;f=fopen(path,"r");if(!f)return 0;while(fgets(line,sizeof(line),f)){bt_frontend_key key;scripted_input *grown;size_t needed,new_cap;if(!strchr(line,'\n')&&!feof(f))goto done;if(line[0]=='#'||line[0]=='\n'||line[0]=='\r')continue;if(sscanf(line,"%llu %31s %llu",&frame,name,&duration)!=3||!duration||frame>ULLONG_MAX-duration||!scripted_key(name,&key))goto done;if(used>SIZE_MAX-2u)goto done;needed=used+2u;if(needed>cap){new_cap=cap?cap:32u;while(new_cap<needed){if(new_cap>SIZE_MAX/2u)goto done;new_cap*=2u;}if(new_cap>SIZE_MAX/sizeof(*items))goto done;grown=(scripted_input*)realloc(items,new_cap*sizeof(*items));if(!grown)goto done;items=grown;cap=new_cap;}items[used++]=(scripted_input){(uint64_t)frame,key,1};items[used++]=(scripted_input){(uint64_t)(frame+duration),key,0};}if(ferror(f))goto done;if(used>1u)qsort(items,used,sizeof(*items),compare_scripted_input);*out=items;*count=used;items=NULL;ok=1;done:if(f)fclose(f);free(items);return ok;}

static const uint32_t pal[16]={
 0xff000000u,0xffffffffu,0xff68372bu,0xff70a4b2u,
 0xff6f3d86u,0xff588d43u,0xff352879u,0xffb8c76fu,
 0xff6f4f25u,0xff433900u,0xff9a6759u,0xff444444u,
 0xff6c6c6cu,0xff9ad284u,0xff6c5eb5u,0xff959595u
};
static int read_file(const char *path,file_buf *b){FILE*f;long n;size_t alloc_size;if(!path||!b)return 0;memset(b,0,sizeof(*b));f=fopen(path,"rb");if(!f)return 0;if(fseek(f,0,SEEK_END)||((n=ftell(f))<0)||fseek(f,0,SEEK_SET)){fclose(f);return 0;}alloc_size=n>0?(size_t)n:(size_t)1;b->p=(uint8_t*)malloc(alloc_size);if(!b->p){fclose(f);return 0;}b->n=(size_t)n;if(b->n&&fread(b->p,1,b->n,f)!=b->n){free(b->p);memset(b,0,sizeof(*b));fclose(f);return 0;}fclose(f);return 1;}
static void free_file(file_buf*b){if(b){free(b->p);memset(b,0,sizeof(*b));}}
static int replace_file(const char *from,const char *to){
#ifdef _WIN32
 return MoveFileExA(from,to,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
 return rename(from,to)==0;
#endif
}
static int write_atomic(const char *path,const void *data,size_t size){char *tmp;FILE*f;size_t n=strlen(path);int ok=0,write_ok=0,close_ok=0;tmp=(char*)malloc(n+5u);if(!tmp)return 0;memcpy(tmp,path,n);memcpy(tmp+n,".tmp",5u);f=fopen(tmp,"wb");if(f){write_ok=(!size||fwrite(data,1,size,f)==size)&&fflush(f)==0;close_ok=fclose(f)==0;if(write_ok&&close_ok&&replace_file(tmp,path))ok=1;}if(!ok)remove(tmp);free(tmp);return ok;}
static int dir_ensure(const char *path){if(BT_MKDIR(path)==0||errno==EEXIST)return 1;return 0;}
static char *join2(const char *a,const char *b){size_t na=strlen(a),nb=strlen(b);int sep=na&&a[na-1]!=BT_SEP;char*r=(char*)malloc(na+(size_t)sep+nb+1u);if(!r)return NULL;memcpy(r,a,na);if(sep)r[na++]=BT_SEP;memcpy(r+na,b,nb+1u);return r;}
static char *default_save_dir(void){char *pref=SDL_GetPrefPath("Static Recomp","BattleTech C64"),*d;if(!pref)return NULL;d=join2(pref,"Saves");SDL_free(pref);if(!d||!dir_ensure(d)){free(d);return NULL;}return d;}
static void print_help(const char *exe){
 printf("BattleTech C64 static recomp frontend\n\n");
 printf("Usage: %s --basic FILE --kernal FILE --chargen FILE --drive-rom FILE --disk1 FILE --disk2 FILE [options]\n",exe);
 printf("Options: --save-dir DIR  --fullscreen  --mute  --frames N  --print-save-path\n");
 printf("Headless automation: --headless --screenshot-dir DIR --screenshot-every N --input-script FILE [--turbo]\n");
 printf("Scripted menu input: --space-at-frame N [--space-every N]\n");
 printf("Input-script lines: FRAME up|down|left|right|fire|space|return|y|n DURATION\n\n");
 printf("Keyboard: arrows=joystick port 2, Left/Right Ctrl or Z=Fire, Space=C64 Space, Enter=C64 Return, P=pause, M=mute, F11=fullscreen, Esc=exit.\n");
}
static int parse_args(int argc,char **argv,options *o){int i;memset(o,0,sizeof(*o));o->volume=70;o->audio_latency=60;o->input_source=2;o->deadzone=35;o->key_up=VK_UP;o->key_down=VK_DOWN;o->key_left=VK_LEFT;o->key_right=VK_RIGHT;o->key_fire='Z';o->key_space=VK_SPACE;o->key_return=VK_RETURN;for(i=1;i<argc;i++){
 const char*a=argv[i];
 #define NEED() do{if(i+1>=argc)return 0;}while(0)
 if(!strcmp(a,"--input-script")){NEED();o->input_script=argv[++i];continue;}
 if(!strcmp(a,"--headless")){o->headless=1;continue;}
 if(!strcmp(a,"--turbo")){o->turbo=1;continue;}
 if(!strcmp(a,"--select-side-at-frame")){char*e=NULL;unsigned long long v;NEED();errno=0;v=strtoull(argv[++i],&e,10);if(errno||!e||*e||!v)return 0;o->side_at_frame=(uint64_t)v;continue;}
 if(!strcmp(a,"--select-side")){char*e=NULL;long v;NEED();errno=0;v=strtol(argv[++i],&e,10);if(errno||!e||*e||v<1||v>2)return 0;o->side_value=(int)v;continue;}
 if(!strcmp(a,"--game"))continue;else if(!strcmp(a,"--basic")){NEED();o->basic=argv[++i];}else if(!strcmp(a,"--kernal")){NEED();o->kernal=argv[++i];}else if(!strcmp(a,"--chargen")){NEED();o->chargen=argv[++i];}else if(!strcmp(a,"--drive-rom")){NEED();o->drive=argv[++i];}else if(!strcmp(a,"--disk1")){NEED();o->disk1=argv[++i];}else if(!strcmp(a,"--disk2")){NEED();o->disk2=argv[++i];}else if(!strcmp(a,"--save-dir")){NEED();o->save_dir=argv[++i];}else if(!strcmp(a,"--screenshot-dir")){NEED();o->screenshot_dir=argv[++i];}else if(!strcmp(a,"--launcher-hwnd")){char*e=NULL;NEED();errno=0;o->launcher_hwnd=strtoull(argv[++i],&e,10);if(errno||!e||*e||!o->launcher_hwnd)return 0;}else if(!strcmp(a,"--volume")||!strcmp(a,"--audio-latency")||!strcmp(a,"--input-source")||!strcmp(a,"--deadzone")||!strcmp(a,"--scale")||!strcmp(a,"--key-up")||!strcmp(a,"--key-down")||!strcmp(a,"--key-left")||!strcmp(a,"--key-right")||!strcmp(a,"--key-fire")||!strcmp(a,"--key-space")||!strcmp(a,"--key-return")){const char*kind=a;char*e=NULL;long v;NEED();errno=0;v=strtol(argv[++i],&e,10);if(errno||!e||*e||v<0||v>65535)return 0;if(!strcmp(kind,"--volume"))o->volume=(int)v;else if(!strcmp(kind,"--audio-latency"))o->audio_latency=(int)v;else if(!strcmp(kind,"--input-source"))o->input_source=(int)v;else if(!strcmp(kind,"--deadzone"))o->deadzone=(int)v;else if(!strcmp(kind,"--scale"))o->scale=(int)v;else if(!strcmp(kind,"--key-up"))o->key_up=(int)v;else if(!strcmp(kind,"--key-down"))o->key_down=(int)v;else if(!strcmp(kind,"--key-left"))o->key_left=(int)v;else if(!strcmp(kind,"--key-right"))o->key_right=(int)v;else if(!strcmp(kind,"--key-fire"))o->key_fire=(int)v;else if(!strcmp(kind,"--key-space"))o->key_space=(int)v;else o->key_return=(int)v;}else if(!strcmp(a,"--frames")||!strcmp(a,"--screenshot-every")||!strcmp(a,"--space-at-frame")||!strcmp(a,"--space-every")){const char*kind=a;char*e=NULL;unsigned long long v;NEED();errno=0;v=strtoull(argv[++i],&e,10);if(errno||!e||*e||v==0u)return 0;if(!strcmp(kind,"--frames"))o->frame_limit=(uint64_t)v;else if(!strcmp(kind,"--screenshot-every"))o->screenshot_every=(uint64_t)v;else if(!strcmp(kind,"--space-at-frame"))o->space_at_frame=(uint64_t)v;else o->space_every=(uint64_t)v;}else if(!strcmp(a,"--fullscreen"))o->fullscreen=1;else if(!strcmp(a,"--mute"))o->mute=1;else if(!strcmp(a,"--correct-aspect"))o->correct_aspect=1;else if(!strcmp(a,"--vsync"))o->vsync=1;else if(!strcmp(a,"--pause-on-focus-loss"))o->pause_focus_loss=1;else if(!strcmp(a,"--print-save-path"))o->print_save_path=1;else if(!strcmp(a,"--help")||!strcmp(a,"-h"))return -1;else return 0;
 }
 return 1;
}
static int media_sync(bt_static_core *core,const char *path,media_cache *cache,int force){size_t n;uint8_t *buf;int err;n=bt_static_core_persistent_media_size(core);if(!n)return 1;buf=(uint8_t*)malloc(n);if(!buf)return 0;err=bt_static_core_persistent_media_export(core,buf,n);if(err!=BT_STATIC_OK){free(buf);return 0;}if(!force&&cache->size==n&&cache->bytes&&!memcmp(cache->bytes,buf,n)){free(buf);return 1;}if(!write_atomic(path,buf,n)){free(buf);return 0;}free(cache->bytes);cache->bytes=buf;cache->size=n;return 1;}
static int media_import_if_present(bt_static_core *core,const char *path,media_cache *cache){file_buf b={0};int err;FILE*f=fopen(path,"rb");if(!f){if(errno==ENOENT)return media_sync(core,path,cache,1);return 0;}fclose(f);if(!read_file(path,&b))return 0;err=bt_static_core_persistent_media_import(core,b.p,b.n);if(err!=BT_STATIC_OK){fprintf(stderr,"Save media import failed: %s (%d). File left untouched: %s\n",bt_static_error_name((bt_static_error)err),err,path);free_file(&b);return 0;}cache->bytes=b.p;cache->size=b.n;return 1;}
static int key_now(const Uint8*k,int n,int sc){return k&&sc>=0&&sc<n?k[sc]!=0:0;}
static Uint16 audio_samples_for_latency(int milliseconds){unsigned target=(unsigned)(BT_STATIC_CORE_AUDIO_SAMPLE_RATE*(milliseconds>0?milliseconds:60)/1000),value=256u;while(value<target&&value<8192u)value*=2u;return (Uint16)value;}
static SDL_AudioDeviceID open_audio_output(int latency){SDL_AudioSpec want,have;SDL_AudioDeviceID audio;memset(&want,0,sizeof(want));want.freq=BT_STATIC_CORE_AUDIO_SAMPLE_RATE;want.format=AUDIO_S16LSB;want.channels=1;want.samples=audio_samples_for_latency(latency);audio=SDL_OpenAudioDevice(NULL,0,&want,&have,0);if(!audio){fprintf(stderr,"Audio disabled: %s\n",SDL_GetError());return 0;}if(have.freq!=BT_STATIC_CORE_AUDIO_SAMPLE_RATE||have.format!=AUDIO_S16LSB||have.channels!=1u){fprintf(stderr,"Audio disabled: host did not provide exact 44100 Hz mono S16 format.\n");SDL_CloseAudioDevice(audio);return 0;}return audio;}
static int submit_key(bt_static_core *core,bt_frontend_input_state *in,bt_frontend_key key,int down){int e=bt_frontend_input_submit(core,in,key,down);if(e!=BT_STATIC_OK)fprintf(stderr,"Input %s failed: %s\n",bt_frontend_key_name(key),bt_static_error_name((bt_static_error)e));return e==BT_STATIC_OK;}
static int edge(int now,int *old){int changed=now!=*old;*old=now;return changed;}
static int keysym_vk(const SDL_Keysym *key){int32_t sym=key->sym;if(key->scancode==SDL_SCANCODE_LCTRL||key->scancode==SDL_SCANCODE_RCTRL)return VK_CONTROL;if(sym>='a'&&sym<='z')return (int)(sym-'a'+'A');if(sym>=0&&sym<=255)return (int)sym;if(sym==(int32_t)(0x40000000u|SDL_SCANCODE_UP))return VK_UP;if(sym==(int32_t)(0x40000000u|SDL_SCANCODE_DOWN))return VK_DOWN;if(sym==(int32_t)(0x40000000u|SDL_SCANCODE_LEFT))return VK_LEFT;if(sym==(int32_t)(0x40000000u|SDL_SCANCODE_RIGHT))return VK_RIGHT;return 0;}
static int binding_action(const options *o,int vk){if(vk==o->key_up)return 0;if(vk==o->key_down)return 1;if(vk==o->key_left)return 2;if(vk==o->key_right)return 3;if(vk==o->key_fire)return 4;if(vk==o->key_space)return 5;if(vk==o->key_return)return 6;return -1;}
static void hex32(const uint8_t d[32],char o[65]){static const char h[]="0123456789abcdef";unsigned i;for(i=0;i<32u;i++){o[i*2u]=h[d[i]>>4];o[i*2u+1u]=h[d[i]&15u];}o[64]=0;}
static int write_ppm(const char *dir,uint64_t frame,const uint32_t *argb){char name[96],*path;FILE*f;size_t i;unsigned char rgb[3];snprintf(name,sizeof(name),"battletech-frame-%06" PRIu64 ".ppm",frame);path=join2(dir,name);if(!path)return 0;f=fopen(path,"wb");if(!f){free(path);return 0;}fprintf(f,"P6\n%d %d\n255\n",BT_STATIC_CORE_FRAME_WIDTH,BT_STATIC_CORE_FRAME_HEIGHT);for(i=0;i<(size_t)BT_STATIC_CORE_FRAME_WIDTH*BT_STATIC_CORE_FRAME_HEIGHT;i++){rgb[0]=(unsigned char)(argb[i]>>16);rgb[1]=(unsigned char)(argb[i]>>8);rgb[2]=(unsigned char)argb[i];if(fwrite(rgb,1,3,f)!=3u){fclose(f);free(path);return 0;}}if(fclose(f)!=0){free(path);return 0;}printf("SCREENSHOT frame=%" PRIu64 " file=%s\n",frame,path);fflush(stdout);free(path);return 1;}
static int snapshot_save(bt_static_core*core,const char*path){size_t n=bt_static_core_snapshot_size(core);uint8_t*data;if(!n)return 0;data=(uint8_t*)malloc(n);if(!data)return 0;if(bt_static_core_snapshot_save(core,data,n)!=BT_STATIC_OK){free(data);return 0;}if(!write_atomic(path,data,n)){free(data);return 0;}free(data);return 1;}
static int snapshot_load(bt_static_core*core,const char*path){file_buf data={0};int ok;if(!read_file(path,&data))return 0;ok=bt_static_core_snapshot_load(core,data.p,data.n)==BT_STATIC_OK;free_file(&data);return ok;}
#ifdef _WIN32
static HWND window_handle(SDL_Window *window){SDL_SysWMinfo info;if(!window)return NULL;SDL_VERSION(&info.version);if(!SDL_GetWindowWMInfo(window,&info)||info.subsystem!=SDL_SYSWM_WINDOWS)return NULL;return info.info.win.window;}
#endif
static SDL_GameController *open_first_controller(void){int j,n=SDL_NumJoysticks();for(j=0;j<n;j++)if(SDL_IsGameController(j)){SDL_GameController *controller=SDL_GameControllerOpen(j);if(controller){printf("GAMEPAD %s\n",SDL_GameControllerName(controller)?SDL_GameControllerName(controller):"connected");fflush(stdout);return controller;}}return NULL;}

int battletech_game_main(int argc,char **argv){
 options o;int pr=parse_args(argc,argv,&o);char *save_dir=NULL,*save_path=NULL,*snapshot_path=NULL;file_buf basic={0},kernal={0},chargen={0},drive={0},d0={0},d1={0};bt_static_create_info ci;bt_static_system_rom_set roms;bt_static_media_pair media;bt_static_core *core=NULL;bt_frontend_input_state in;media_cache saves={0};SDL_Window*w=NULL;SDL_Renderer*r=NULL;SDL_Texture*t=NULL;SDL_GameController*pad=NULL;SDL_AudioDeviceID audio=0;uint32_t argb[BT_STATIC_CORE_FRAME_WIDTH*BT_STATIC_CORE_FRAME_HEIGHT]={0};int16_t pcm[4096];int running=1,paused=0,focus_paused=0,muted=0,fullscreen=0,frame_valid=0,capture_requested=0,rc=1,script_space_down=0,script_space_done=0;Uint64 next_tick=0;uint64_t last_frame=0,next_script_space=0;int old_up=0,old_down=0,old_left=0,old_right=0,old_fire=0,old_space=0,old_return=0,old_pause=0,old_mute=0,old_fs=0,old_help=0,old_escape=0;
#ifdef _WIN32
 HANDLE resume_event=NULL,save_event=NULL,load_event=NULL,capture_event=NULL,audio_event=NULL,audio_mapping=NULL;bt_frontend_audio_config*audio_config=NULL;HWND launcher_hwnd=(HWND)(uintptr_t)o.launcher_hwnd,game_hwnd=NULL;char event_name[128];
#endif
 scripted_input *scripted_inputs=NULL;size_t scripted_input_count=0,scripted_input_index=0;int scripted_side_done=0;
 int logical_width=o.correct_aspect?(BT_STATIC_CORE_FRAME_HEIGHT*4/3):BT_STATIC_CORE_FRAME_WIDTH;
 int display_scale=o.scale>0?o.scale:3,window_width=logical_width*display_scale,window_height=BT_STATIC_CORE_FRAME_HEIGHT*display_scale;
 if(pr<0){print_help(argv[0]);return 0;}if(!pr){print_help(argv[0]);return 2;}
 SDL_SetMainReady();
 {Uint32 flags=SDL_INIT_TIMER|SDL_INIT_EVENTS;if(!o.headless)flags|=SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER;if(SDL_Init(flags)!=0){fprintf(stderr,"SDL init failed: %s\n",SDL_GetError());return 2;}}
 if(o.save_dir){save_dir=strdup(o.save_dir);if(!save_dir||!dir_ensure(save_dir)){fprintf(stderr,"Cannot create save directory: %s\n",o.save_dir);goto out;}}else save_dir=default_save_dir();
 if(!save_dir){fprintf(stderr,"Cannot resolve save directory: %s\n",SDL_GetError());goto out;}save_path=join2(save_dir,"battletech-save-media.btpm");if(!save_path)goto out;snapshot_path=join2(save_dir,"Quick Save.btstate");if(!snapshot_path)goto out;
 if(o.screenshot_dir&&!dir_ensure(o.screenshot_dir)){fprintf(stderr,"Cannot create screenshot directory: %s\n",o.screenshot_dir);goto out;}
 printf("SAVE_FOLDER %s\nSAVE_FILE %s\n",save_dir,save_path);fflush(stdout);
 if(o.print_save_path&&!o.basic&&!o.kernal&&!o.chargen&&!o.drive&&!o.disk1&&!o.disk2){rc=0;goto out;}
 if(!o.basic||!o.kernal||!o.chargen||!o.drive||!o.disk1||!o.disk2){print_help(argv[0]);goto out;}
 if(!read_file(o.basic,&basic)||!read_file(o.kernal,&kernal)||!read_file(o.chargen,&chargen)||!read_file(o.drive,&drive)||!read_file(o.disk1,&d0)||!read_file(o.disk2,&d1)){fprintf(stderr,"Failed to read one or more ROM/media files.\n");goto out;}
 memset(&ci,0,sizeof(ci));ci.struct_size=sizeof(ci);ci.requested_api_version=BT_STATIC_CORE_API_VERSION;core=bt_static_core_create(&ci);if(!core){fprintf(stderr,"Core creation failed.\n");goto out;}
 memset(&roms,0,sizeof(roms));roms.basic.data=basic.p;roms.basic.size=basic.n;roms.kernal.data=kernal.p;roms.kernal.size=kernal.n;roms.chargen.data=chargen.p;roms.chargen.size=chargen.n;roms.drive1541ii.data=drive.p;roms.drive1541ii.size=drive.n;
 memset(&media,0,sizeof(media));media.side[0].data=d0.p;media.side[0].size=d0.n;media.side[1].data=d1.p;media.side[1].size=d1.n;
 if(bt_static_core_load_system_roms(core,&roms)!=BT_STATIC_OK||bt_static_core_load_media_pair(core,&media)!=BT_STATIC_OK||bt_static_core_reset(core,0)!=BT_STATIC_OK){fprintf(stderr,"Core media/ROM validation or reset failed: %s\n",bt_static_core_last_diagnostic(core)->message);goto out;}
 if(!media_import_if_present(core,save_path,&saves))goto out;
 bt_frontend_input_init(&in);next_script_space=o.space_at_frame;muted=o.mute;fullscreen=o.fullscreen;
 if(o.input_script&&!load_input_script(o.input_script,&scripted_inputs,&scripted_input_count)){fprintf(stderr,"Invalid input script: %s\n",o.input_script);goto out;}
#ifdef _WIN32
 if(launcher_hwnd){snprintf(event_name,sizeof(event_name),"Local\\BattleTechC64Resume-%lu",(unsigned long)GetCurrentProcessId());resume_event=CreateEventA(NULL,FALSE,FALSE,event_name);snprintf(event_name,sizeof(event_name),"Local\\BattleTechC64Save-%lu",(unsigned long)GetCurrentProcessId());save_event=CreateEventA(NULL,FALSE,FALSE,event_name);snprintf(event_name,sizeof(event_name),"Local\\BattleTechC64Load-%lu",(unsigned long)GetCurrentProcessId());load_event=CreateEventA(NULL,FALSE,FALSE,event_name);snprintf(event_name,sizeof(event_name),"Local\\BattleTechC64Capture-%lu",(unsigned long)GetCurrentProcessId());capture_event=CreateEventA(NULL,FALSE,FALSE,event_name);snprintf(event_name,sizeof(event_name),"Local\\BattleTechC64Audio-%lu",(unsigned long)GetCurrentProcessId());audio_event=CreateEventA(NULL,FALSE,FALSE,event_name);snprintf(event_name,sizeof(event_name),"Local\\BattleTechC64AudioConfig-%lu",(unsigned long)GetCurrentProcessId());audio_mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,sizeof(*audio_config),event_name);if(audio_mapping)audio_config=(bt_frontend_audio_config*)MapViewOfFile(audio_mapping,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(*audio_config));if(audio_config){audio_config->enabled=o.mute?0:1;audio_config->volume=o.volume;audio_config->latency_ms=o.audio_latency;}if(!resume_event||!save_event||!load_event||!capture_event||!audio_event||!audio_mapping||!audio_config){fprintf(stderr,"Launcher communication setup failed: Windows error %lu.\n",(unsigned long)GetLastError());goto out;}}
#endif
 if(!o.headless){int used_fallback=0;SDL_SetHint("SDL_RENDER_SCALE_QUALITY","0");w=SDL_CreateWindow(BT_GAME_TITLE,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,window_width,window_height,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);if(!w){fprintf(stderr,"Window creation failed: %s\n",SDL_GetError());goto out;}
#ifdef _WIN32
  game_hwnd=window_handle(w);if(launcher_hwnd&&!game_hwnd){fprintf(stderr,"Could not identify the game window.\n");goto out;}
#endif
  r=o.vsync?SDL_CreateRenderer(w,-1,SDL_RENDERER_ACCELERATED|SDL_RENDERER_PRESENTVSYNC):SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);if(!r){used_fallback=1;r=SDL_CreateRenderer(w,-1,o.vsync?SDL_RENDERER_SOFTWARE:SDL_RENDERER_ACCELERATED);}if(!r){fprintf(stderr,"Renderer creation failed: %s\n",SDL_GetError());goto out;}if(used_fallback)fprintf(stderr,"Warning: requested renderer unavailable; using a fallback%s.\n",o.vsync?" without VSync":"");if(SDL_RenderSetLogicalSize(r,logical_width,BT_STATIC_CORE_FRAME_HEIGHT)!=0||SDL_SetRenderDrawColor(r,0,0,0,255)!=0){fprintf(stderr,"Renderer setup failed: %s\n",SDL_GetError());goto out;}
  t=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,BT_STATIC_CORE_FRAME_WIDTH,BT_STATIC_CORE_FRAME_HEIGHT);if(!t){fprintf(stderr,"Texture creation failed: %s\n",SDL_GetError());goto out;}if(fullscreen&&SDL_SetWindowFullscreen(w,SDL_WINDOW_FULLSCREEN_DESKTOP)!=0){fprintf(stderr,"Full-screen startup failed: %s\n",SDL_GetError());goto out;}}
 if(!o.headless&&!o.mute){audio=open_audio_output(o.audio_latency);if(audio)SDL_PauseAudioDevice(audio,0);}
 if(!o.headless&&o.input_source!=0)pad=open_first_controller();

 printf("CONTROLS arrows=joystick2 ctrl-or-z=fire space=c64-space enter=c64-return p=pause m=mute f11=fullscreen escape=switch-or-exit\n");fflush(stdout);next_tick=SDL_GetTicks64();
 while(running){SDL_Event ev;const Uint8 *keys;int nk=0;bt_static_run_result rr;bt_static_video_frame vf;size_t avail,got;int ku,kd,kl,kr,kf,ks,ke,kp,km,kx,kh;unsigned key_down_events=0,key_up_events=0,pulse_release=0;
  if(!scripted_side_done&&o.side_at_frame&&last_frame>=o.side_at_frame){if(!o.side_value||bt_static_core_select_disk_side(core,(unsigned)o.side_value)!=BT_STATIC_OK){fprintf(stderr,"Scripted disk-side selection failed.\n");goto out;}scripted_side_done=1;printf("SCRIPT_MEDIA frame=%" PRIu64 " side=%d\n",last_frame,o.side_value);}
  while(scripted_input_index<scripted_input_count&&scripted_inputs[scripted_input_index].frame<=last_frame){scripted_input *si=&scripted_inputs[scripted_input_index++];if(!submit_key(core,&in,si->key,si->down))goto out;printf("SCRIPT_INPUT frame=%" PRIu64 " key=%s state=%s\n",last_frame,bt_frontend_key_name(si->key),si->down?"down":"up");}
  int escape_requested=0;while(SDL_PollEvent(&ev)){if(ev.type==SDL_QUIT)running=0;else if(ev.type==SDL_WINDOWEVENT&&ev.window.event==SDL_WINDOWEVENT_FOCUS_LOST&&o.pause_focus_loss&&!paused){paused=1;focus_paused=1;if(audio)SDL_PauseAudioDevice(audio,1);}else if(ev.type==SDL_WINDOWEVENT&&ev.window.event==SDL_WINDOWEVENT_FOCUS_GAINED&&focus_paused){paused=0;focus_paused=0;if(audio)SDL_PauseAudioDevice(audio,0);}else if(ev.type==SDL_CONTROLLERDEVICEADDED&&!pad&&o.input_source!=0){pad=SDL_GameControllerOpen(ev.cdevice.which);if(pad){printf("GAMEPAD %s\n",SDL_GameControllerName(pad)?SDL_GameControllerName(pad):"connected");fflush(stdout);}}else if(ev.type==SDL_CONTROLLERDEVICEREMOVED&&pad&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(pad))==ev.cdevice.which){SDL_GameControllerClose(pad);pad=NULL;pad=open_first_controller();}else if((ev.type==SDL_KEYDOWN||ev.type==SDL_KEYUP)&&(!ev.key.repeat||ev.type==SDL_KEYUP)){int action=binding_action(&o,keysym_vk(&ev.key.keysym));if(ev.type==SDL_KEYDOWN&&ev.key.keysym.scancode==SDL_SCANCODE_ESCAPE)escape_requested=1;if(action>=0){if(ev.type==SDL_KEYDOWN)key_down_events|=1u<<(unsigned)action;else key_up_events|=1u<<(unsigned)action;}}}keys=o.headless?NULL:SDL_GetKeyboardState(&nk);
#ifdef _WIN32
  if(resume_event&&WaitForSingleObject(resume_event,0)==WAIT_OBJECT_0){if(w){SDL_ShowWindow(w);SDL_RaiseWindow(w);if(SDL_SetWindowInputFocus(w)!=0)fprintf(stderr,"Window focus request failed: %s\n",SDL_GetError());}if(game_hwnd){ShowWindow(game_hwnd,SW_SHOW);BringWindowToTop(game_hwnd);SetForegroundWindow(game_hwnd);}paused=0;focus_paused=0;if(audio)SDL_PauseAudioDevice(audio,0);}
  if(save_event&&WaitForSingleObject(save_event,0)==WAIT_OBJECT_0){printf("SNAPSHOT_SAVE %s %s\n",snapshot_path,snapshot_save(core,snapshot_path)?"OK":"FAILED");fflush(stdout);}
  if(load_event&&WaitForSingleObject(load_event,0)==WAIT_OBJECT_0){int loaded=snapshot_load(core,snapshot_path);if(loaded)loaded=bt_frontend_input_reset(core,&in)==BT_STATIC_OK;if(loaded){old_up=old_down=old_left=old_right=old_fire=old_space=old_return=0;script_space_down=0;}printf("SNAPSHOT_LOAD %s %s\n",snapshot_path,loaded?"OK":"FAILED");fflush(stdout);}
  if(capture_event&&WaitForSingleObject(capture_event,0)==WAIT_OBJECT_0&&o.screenshot_dir)capture_requested=1;
  if(audio_event&&WaitForSingleObject(audio_event,0)==WAIT_OBJECT_0&&audio_config){int enabled=(int)audio_config->enabled,volume=(int)audio_config->volume,latency=(int)audio_config->latency_ms;if(volume<0||volume>100)volume=70;if(latency!=20&&latency!=40&&latency!=60&&latency!=80&&latency!=120&&latency!=250)latency=60;if(audio&&(!enabled||latency!=o.audio_latency)){SDL_CloseAudioDevice(audio);audio=0;}o.volume=volume;o.audio_latency=latency;if(enabled){if(!audio&&!o.headless)audio=open_audio_output(o.audio_latency);muted=0;if(audio)SDL_PauseAudioDevice(audio,paused?1:0);}else{muted=1;bt_static_core_audio_clear(core);}printf("AUDIO_CONFIG enabled=%d volume=%d latency=%d\n",enabled?1:0,o.volume,o.audio_latency);fflush(stdout);}
#endif
  {int escape_now=escape_requested||key_now(keys,nk,SDL_SCANCODE_ESCAPE);if(edge(escape_now,&old_escape)&&escape_now){
#ifdef _WIN32
   if(launcher_hwnd&&IsWindow(launcher_hwnd)){DWORD launcher_pid=0;GetWindowThreadProcessId(launcher_hwnd,&launcher_pid);if(launcher_pid)AllowSetForegroundWindow(launcher_pid);paused=1;if(audio)SDL_PauseAudioDevice(audio,1);if(game_hwnd)ShowWindow(game_hwnd,SW_HIDE);ShowWindow(launcher_hwnd,SW_RESTORE);BringWindowToTop(launcher_hwnd);SetForegroundWindow(launcher_hwnd);PostMessageW(launcher_hwnd,BT_WM_GAME_RETURN,0,0);continue;}
#endif
   running=0;continue;}}
#ifdef _WIN32
   if(!o.headless&&o.input_source!=1){ku=(GetAsyncKeyState(o.key_up)&0x8000)!=0;kd=(GetAsyncKeyState(o.key_down)&0x8000)!=0;kl=(GetAsyncKeyState(o.key_left)&0x8000)!=0;kr=(GetAsyncKeyState(o.key_right)&0x8000)!=0;kf=(GetAsyncKeyState(o.key_fire)&0x8000)!=0||(GetAsyncKeyState(VK_CONTROL)&0x8000)!=0;ks=(GetAsyncKeyState(o.key_space)&0x8000)!=0;ke=(GetAsyncKeyState(o.key_return)&0x8000)!=0;}else ku=kd=kl=kr=kf=ks=ke=0;
#else
  ku=key_now(keys,nk,SDL_SCANCODE_UP);kd=key_now(keys,nk,SDL_SCANCODE_DOWN);kl=key_now(keys,nk,SDL_SCANCODE_LEFT);kr=key_now(keys,nk,SDL_SCANCODE_RIGHT);kf=key_now(keys,nk,SDL_SCANCODE_LCTRL)||key_now(keys,nk,SDL_SCANCODE_RCTRL)||key_now(keys,nk,SDL_SCANCODE_Z);ks=key_now(keys,nk,SDL_SCANCODE_SPACE);ke=key_now(keys,nk,SDL_SCANCODE_RETURN);
#endif
  if(key_down_events&1u)ku=1;
  if((key_up_events&1u)&&!(key_down_events&1u))ku=0;
  if(key_down_events&2u)kd=1;
  if((key_up_events&2u)&&!(key_down_events&2u))kd=0;
  if(key_down_events&4u)kl=1;
  if((key_up_events&4u)&&!(key_down_events&4u))kl=0;
  if(key_down_events&8u)kr=1;
  if((key_up_events&8u)&&!(key_down_events&8u))kr=0;
  if(key_down_events&16u)kf=1;
  if((key_up_events&16u)&&!(key_down_events&16u))kf=0;
  if(key_down_events&32u)ks=1;
  if((key_up_events&32u)&&!(key_down_events&32u))ks=0;
  if(key_down_events&64u)ke=1;
  if((key_up_events&64u)&&!(key_down_events&64u))ke=0;
  pulse_release=key_down_events&key_up_events;
  kp=key_now(keys,nk,SDL_SCANCODE_P);km=key_now(keys,nk,SDL_SCANCODE_M);kx=key_now(keys,nk,SDL_SCANCODE_F11);kh=key_now(keys,nk,SDL_SCANCODE_F1);
  if(pad&&o.input_source!=0){int threshold=32767*o.deadzone/100;int16_t ax=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTX),ay=SDL_GameControllerGetAxis(pad,SDL_CONTROLLER_AXIS_LEFTY);ku=ku||SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_DPAD_UP)||ay<-threshold;kd=kd||SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_DPAD_DOWN)||ay>threshold;kl=kl||SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_DPAD_LEFT)||ax<-threshold;kr=kr||SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_DPAD_RIGHT)||ax>threshold;kf=kf||SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_A);ks=ks||SDL_GameControllerGetButton(pad,SDL_CONTROLLER_BUTTON_START);}
  if(edge(ku,&old_up)&&!submit_key(core,&in,BT_FRONTEND_KEY_UP,ku))goto out;
  if(edge(kd,&old_down)&&!submit_key(core,&in,BT_FRONTEND_KEY_DOWN,kd))goto out;
  if(edge(kl,&old_left)&&!submit_key(core,&in,BT_FRONTEND_KEY_LEFT,kl))goto out;
  if(edge(kr,&old_right)&&!submit_key(core,&in,BT_FRONTEND_KEY_RIGHT,kr))goto out;
  if(edge(kf,&old_fire)&&!submit_key(core,&in,BT_FRONTEND_KEY_FIRE,kf))goto out;
  if(edge(ks,&old_space)&&!submit_key(core,&in,BT_FRONTEND_KEY_MENU_SPACE,ks))goto out;
  if(edge(ke,&old_return)&&!submit_key(core,&in,BT_FRONTEND_KEY_C64_RETURN,ke))goto out;
  if(edge(kp,&old_pause)&&kp){paused=!paused;focus_paused=0;if(audio)SDL_PauseAudioDevice(audio,paused?1:0);}if(edge(km,&old_mute)&&km){muted=!muted;if(audio&&muted)SDL_ClearQueuedAudio(audio);}if(edge(kx,&old_fs)&&kx&&w){Uint32 target=fullscreen?0u:SDL_WINDOW_FULLSCREEN_DESKTOP;if(SDL_SetWindowFullscreen(w,target)==0)fullscreen=!fullscreen;else fprintf(stderr,"Full-screen change failed: %s\n",SDL_GetError());}
  if(edge(kh,&old_help)&&kh){printf("HELP arrows=joystick2 ctrl-or-z=fire space=c64-space/menu enter=c64-return gamepad-dpad-or-stick=move gamepad-a=fire gamepad-start=space p=pause m=mute f11=fullscreen escape=switch-or-exit save=%s\n",save_path);fflush(stdout);}
  if(!paused){if(next_script_space&&!script_space_done&&last_frame>=next_script_space){if(!script_space_down){if(!submit_key(core,&in,BT_FRONTEND_KEY_MENU_SPACE,1))goto out;script_space_down=1;printf("SCRIPT_INPUT frame=%" PRIu64 " key=space state=down\n",last_frame);fflush(stdout);}else{if(!submit_key(core,&in,BT_FRONTEND_KEY_MENU_SPACE,0))goto out;script_space_down=0;printf("SCRIPT_INPUT frame=%" PRIu64 " key=space state=up\n",last_frame);fflush(stdout);if(o.space_every)next_script_space+=o.space_every;else script_space_done=1;}}int e=bt_static_core_advance_frame(core,4000000u,&rr);if(e!=BT_STATIC_OK&&rr.reason!=BT_STATIC_STOP_FRAME_READY&&rr.reason!=BT_STATIC_STOP_TARGET_REACHED){const bt_static_diagnostic*d=bt_static_core_last_diagnostic(core);fprintf(stderr,"Static core stopped: %s at C64 $%04X drive $%04X cycle=%" PRIu64 "\n",d&&d->message?d->message:bt_static_error_name((bt_static_error)e),rr.c64_pc,rr.drive_pc,rr.end_cycle);goto out;}last_frame=rr.completed_frame;if(pulse_release){if((pulse_release&1u)&&!submit_key(core,&in,BT_FRONTEND_KEY_UP,0))goto out;if((pulse_release&2u)&&!submit_key(core,&in,BT_FRONTEND_KEY_DOWN,0))goto out;if((pulse_release&4u)&&!submit_key(core,&in,BT_FRONTEND_KEY_LEFT,0))goto out;if((pulse_release&8u)&&!submit_key(core,&in,BT_FRONTEND_KEY_RIGHT,0))goto out;if((pulse_release&16u)&&!submit_key(core,&in,BT_FRONTEND_KEY_FIRE,0))goto out;if((pulse_release&32u)&&!submit_key(core,&in,BT_FRONTEND_KEY_MENU_SPACE,0))goto out;if((pulse_release&64u)&&!submit_key(core,&in,BT_FRONTEND_KEY_C64_RETURN,0))goto out;if(pulse_release&1u)old_up=0;if(pulse_release&2u)old_down=0;if(pulse_release&4u)old_left=0;if(pulse_release&8u)old_right=0;if(pulse_release&16u)old_fire=0;if(pulse_release&32u)old_space=0;if(pulse_release&64u)old_return=0;}
   if(bt_static_core_get_video(core,&vf)==BT_STATIC_OK&&vf.pixels){size_t y,x;for(y=0;y<BT_STATIC_CORE_FRAME_HEIGHT;y++){const uint8_t *src=vf.pixels+y*vf.pitch;for(x=0;x<BT_STATIC_CORE_FRAME_WIDTH;x++)argb[y*BT_STATIC_CORE_FRAME_WIDTH+x]=pal[src[x]&15u];}frame_valid=1;if(t&&SDL_UpdateTexture(t,NULL,argb,(int)(BT_STATIC_CORE_FRAME_WIDTH*sizeof(uint32_t)))!=0){fprintf(stderr,"Texture upload failed: %s\n",SDL_GetError());goto out;}if(o.screenshot_dir&&o.screenshot_every&&last_frame%o.screenshot_every==0u&&!write_ppm(o.screenshot_dir,last_frame,argb)){fprintf(stderr,"Screenshot write failed at frame %" PRIu64 "\n",last_frame);goto out;}}
   if(audio&&!muted){avail=bt_static_core_audio_available(core);while(avail){size_t cap=sizeof(pcm)/sizeof(pcm[0]),sample;got=bt_static_core_audio_read(core,pcm,avail<cap?avail:cap);if(!got)break;if(o.volume<100)for(sample=0;sample<got;sample++)pcm[sample]=(int16_t)((int32_t)pcm[sample]*o.volume/100);if(SDL_GetQueuedAudioSize(audio)<(Uint32)(BT_STATIC_CORE_AUDIO_SAMPLE_RATE*sizeof(int16_t)/2u))SDL_QueueAudio(audio,pcm,(Uint32)(got*sizeof(int16_t)));avail-=got;}}else bt_static_core_audio_clear(core);
   if(!media_sync(core,save_path,&saves,0)){fprintf(stderr,"Could not persist save media to %s\n",save_path);goto out;}
   if(o.frame_limit&&last_frame>=o.frame_limit)running=0;
  }
  if(capture_requested&&frame_valid&&o.screenshot_dir){if(!write_ppm(o.screenshot_dir,last_frame,argb))fprintf(stderr,"Game-frame capture failed.\n");capture_requested=0;}
  if(r&&t){if(SDL_RenderClear(r)!=0||SDL_RenderCopy(r,t,NULL,NULL)!=0){fprintf(stderr,"Frame presentation failed: %s\n",SDL_GetError());goto out;}SDL_RenderPresent(r);}
  next_tick+=20u;{Uint64 now=SDL_GetTicks64();if(!o.turbo&&next_tick>now)SDL_Delay((Uint32)(next_tick-now));else if(o.turbo||now-next_tick>200u)next_tick=now;}
 }
 if(core&&!media_sync(core,save_path,&saves,1))fprintf(stderr,"Warning: final save-media flush failed.\n");
 if(core){bt_static_status st;uint8_t dig[32];char hx[65];memset(&st,0,sizeof(st));if(bt_static_core_get_status(core,&st)==BT_STATIC_OK)printf("FINAL_STATUS cycle=%" PRIu64 " frame=%" PRIu64 " c64=%04X drive=%04X side=%u error=%s\n",st.cycle,st.completed_frame,st.c64_pc,st.drive_pc,st.active_disk_side,bt_static_error_name(st.error));if(bt_static_core_state_sha256(core,dig)==BT_STATIC_OK){hex32(dig,hx);printf("STATE_SHA256 %s\n",hx);}fflush(stdout);}
 rc=0;
out:
 free(scripted_inputs);
 if(pad)SDL_GameControllerClose(pad);
 if(audio)SDL_CloseAudioDevice(audio);
 if(t)SDL_DestroyTexture(t);
 if(r)SDL_DestroyRenderer(r);
 if(w)SDL_DestroyWindow(w);
#ifdef _WIN32
 if(resume_event)CloseHandle(resume_event);
 if(save_event)CloseHandle(save_event);
 if(load_event)CloseHandle(load_event);
 if(capture_event)CloseHandle(capture_event);
 if(audio_config)UnmapViewOfFile(audio_config);
 if(audio_mapping)CloseHandle(audio_mapping);
 if(audio_event)CloseHandle(audio_event);
#endif
 if(core)bt_static_core_destroy(core);
 free(saves.bytes);
 free(snapshot_path);
 free(save_path);
 free(save_dir);
 free_file(&basic);free_file(&kernal);free_file(&chargen);free_file(&drive);free_file(&d0);free_file(&d1);
 SDL_Quit();
 return rc;
}
