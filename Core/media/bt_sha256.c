#include "bt_sha256.h"
#include <stdbool.h>
#include <string.h>
static const uint32_t K[64]={
0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
static uint32_t rr(uint32_t x,unsigned n){return (x>>n)|(x<<(32u-n));}
static uint32_t be32(const uint8_t*p){return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];}
static void put32(uint8_t*p,uint32_t x){p[0]=(uint8_t)(x>>24);p[1]=(uint8_t)(x>>16);p[2]=(uint8_t)(x>>8);p[3]=(uint8_t)x;}
static void transform(bt_sha256_ctx*c,const uint8_t*b){uint32_t w[64],a,bv,d,e,f,g,h,t1,t2,cc;unsigned i;for(i=0;i<16;i++)w[i]=be32(b+4*i);for(i=16;i<64;i++){uint32_t x=w[i-15],y=w[i-2];uint32_t s0=rr(x,7)^rr(x,18)^(x>>3),s1=rr(y,17)^rr(y,19)^(y>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}a=c->h[0];bv=c->h[1];cc=c->h[2];d=c->h[3];e=c->h[4];f=c->h[5];g=c->h[6];h=c->h[7];for(i=0;i<64;i++){uint32_t S1=rr(e,6)^rr(e,11)^rr(e,25),ch=(e&f)^((~e)&g);t1=h+S1+ch+K[i]+w[i];uint32_t S0=rr(a,2)^rr(a,13)^rr(a,22),maj=(a&bv)^(a&cc)^(bv&cc);t2=S0+maj;h=g;g=f;f=e;e=d+t1;d=cc;cc=bv;bv=a;a=t1+t2;}c->h[0]+=a;c->h[1]+=bv;c->h[2]+=cc;c->h[3]+=d;c->h[4]+=e;c->h[5]+=f;c->h[6]+=g;c->h[7]+=h;}
void bt_sha256_init(bt_sha256_ctx*c){static const uint32_t H[8]={0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};if(!c)return;memcpy(c->h,H,sizeof(H));c->total=0;c->used=0;}
void bt_sha256_update(bt_sha256_ctx*c,const void*data,size_t size){const uint8_t*p=(const uint8_t*)data;if(!c||(!p&&size))return;c->total+=(uint64_t)size;while(size){size_t n=64u-c->used;if(n>size)n=size;memcpy(c->block+c->used,p,n);c->used+=n;p+=n;size-=n;if(c->used==64u){transform(c,c->block);c->used=0;}}}
void bt_sha256_final(bt_sha256_ctx*c,uint8_t out[32]){uint64_t bits;unsigned i;if(!c||!out)return;bits=c->total*8u;c->block[c->used++]=0x80u;if(c->used>56u){while(c->used<64u)c->block[c->used++]=0;transform(c,c->block);c->used=0;}while(c->used<56u)c->block[c->used++]=0;for(i=0;i<8u;i++)c->block[63u-i]=(uint8_t)(bits>>(8u*i));transform(c,c->block);for(i=0;i<8u;i++)put32(out+4u*i,c->h[i]);memset(c,0,sizeof(*c));}
void bt_sha256(const void*data,size_t size,uint8_t out[32]){bt_sha256_ctx c;bt_sha256_init(&c);bt_sha256_update(&c,data,size);bt_sha256_final(&c,out);}
static int hx(char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
bool bt_sha256_equal_hex(const uint8_t d[32],const char*h){unsigned i;if(!d||!h)return false;for(i=0;i<32u;i++){int a=hx(h[2u*i]),b=hx(h[2u*i+1u]);if(a<0||b<0||d[i]!=(uint8_t)((a<<4)|b))return false;}return h[64]=='\0';}
