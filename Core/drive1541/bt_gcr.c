#include "bt_gcr.h"
#include "../media/bt_d64.h"
#include <string.h>
static const uint8_t enc[16]={0x0au,0x0bu,0x12u,0x13u,0x0eu,0x0fu,0x16u,0x17u,0x09u,0x19u,0x1au,0x1bu,0x0du,0x1du,0x1eu,0x15u};
static int dec5(uint8_t x){unsigned i;for(i=0;i<16u;i++)if(enc[i]==x)return (int)i;return -1;}
bool bt_gcr_encode4(const uint8_t in[4],uint8_t out[5]){uint64_t bits=0;unsigned i;if(!in||!out)return false;for(i=0;i<4u;i++){bits=(bits<<5)|enc[in[i]>>4];bits=(bits<<5)|enc[in[i]&15u];}for(i=0;i<5u;i++)out[4u-i]=(uint8_t)(bits>>(8u*i));return true;}
bool bt_gcr_decode5(const uint8_t in[5],uint8_t out[4]){uint64_t bits=0;unsigned i;if(!in||!out)return false;for(i=0;i<5u;i++)bits=(bits<<8)|in[i];for(i=0;i<4u;i++){unsigned sh=35u-i*10u;int a=dec5((uint8_t)((bits>>sh)&31u)),b=dec5((uint8_t)((bits>>(sh-5u))&31u));if(a<0||b<0)return false;out[i]=(uint8_t)((a<<4)|b);}return true;}
unsigned bt_gcr_track_bytes(unsigned t){if(t<1u||t>35u)return 0u;if(t<=17u)return 7692u;if(t<=24u)return 7142u;if(t<=30u)return 6666u;return 6250u;}
static void encode_bytes(const uint8_t*in,size_t n,uint8_t*out){size_t i;for(i=0;i<n;i+=4u)bt_gcr_encode4(in+i,out+(i/4u)*5u);}
bool bt_gcr_build_sector(uint8_t track,uint8_t sector,uint8_t id1,uint8_t id2,const uint8_t data[256],uint8_t out[361]){uint8_t h[8],d[260];uint8_t cs=0;unsigned i;if(!data||!out||!bt_d64_sectors_on_track(track)||sector>=bt_d64_sectors_on_track(track))return false;memset(out,0x55u,361u);memset(out,0xffu,5u);h[0]=0x08u;h[1]=(uint8_t)(sector^track^id2^id1);h[2]=sector;h[3]=track;h[4]=id2;h[5]=id1;h[6]=0x0fu;h[7]=0x0fu;encode_bytes(h,8u,out+5u);memset(out+15u,0x55u,9u);memset(out+24u,0xffu,5u);d[0]=0x07u;for(i=0;i<256u;i++){d[1u+i]=data[i];cs^=data[i];}d[257]=cs;d[258]=0;d[259]=0;encode_bytes(d,260u,out+29u);memset(out+354u,0x55u,7u);return true;}
bool bt_gcr_build_track(uint8_t track,uint8_t id1,uint8_t id2,const uint8_t *sectors,size_t n,uint8_t*out,size_t cap,size_t*used){unsigned sc=bt_d64_sectors_on_track(track),target=bt_gcr_track_bytes(track),s;size_t pos=0;if(!sc||!sectors||n<(size_t)sc*256u||!out||cap<target)return false;memset(out,0x55u,target);for(s=0;s<sc;s++){uint8_t sec[361];if(!bt_gcr_build_sector(track,(uint8_t)s,id1,id2,sectors+(size_t)s*256u,sec))return false;if(pos+361u>target)return false;memcpy(out+pos,sec,361u);pos+=361u;}/* Remaining nominal rotation bytes are deterministic $55 gap bytes. */if(used)*used=target;return true;}
static uint8_t circ(const uint8_t*p,size_t n,size_t i){return p[i%n];}
static bool decode_circular(const uint8_t*p,size_t n,size_t pos,uint8_t*out,size_t decoded){
 size_t i;uint8_t in5[5],out4[4];if((decoded&3u)!=0u)return false;
 for(i=0;i<decoded;i+=4u){size_t k;for(k=0;k<5u;k++)in5[k]=circ(p,n,pos+(i/4u)*5u+k);if(!bt_gcr_decode5(in5,out4))return false;memcpy(out+i,out4,4u);}return true;
}
static bool sync5(const uint8_t*p,size_t n,size_t pos){size_t i;for(i=0;i<5u;i++)if(circ(p,n,pos+i)!=0xffu)return false;return true;}
bool bt_gcr_extract_track(uint8_t track,const uint8_t *tb,size_t n,uint8_t *sectors,size_t cap,uint32_t *mask){
 size_t i;uint32_t found=0;unsigned sc=bt_d64_sectors_on_track(track);if(!tb||!n||!sectors||cap<(size_t)sc*256u||!mask||!sc)return false;
 for(i=0;i<n;i++){
  uint8_t h[8],d[260],hc,dc,sec,tr;size_t q,limit;
  if(!sync5(tb,n,i)||!decode_circular(tb,n,i+5u,h,8u)||h[0]!=0x08u)continue;
  hc=(uint8_t)(h[2]^h[3]^h[4]^h[5]);if(h[1]!=hc)continue;sec=h[2];tr=h[3];if(tr!=track||sec>=sc)continue;
  q=i+15u;limit=i+80u;for(;q<limit&&!sync5(tb,n,q);q++);if(q==limit)continue;
  if(!decode_circular(tb,n,q+5u,d,260u)||d[0]!=0x07u)continue;
  dc=0;
  for(unsigned k=0;k<256u;k++)dc^=d[1u+k];
  if(d[257]!=dc)continue;
  memcpy(sectors+(size_t)sec*256u,d+1u,256u);found|=(uint32_t)1u<<sec;
 }
 *mask=found;return found!=0u;
}
