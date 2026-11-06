#include "psx_force_handoff.h"

static void put32(uint8_t* p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static uint32_t get32(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

void t3_force_handoff_pack(const t3_force_handoff_t* in,uint8_t* out){
    for(unsigned i=0;i<T3_FORCE_HANDOFF_BYTES;++i)out[i]=0;
    out[0]='G';out[1]='F';out[2]=T3_FORCE_HANDOFF_VERSION;
    out[3]=in->level;out[4]=in->section;out[5]=in->character;out[6]=in->costume;out[7]=in->keys;out[8]=in->flags;
    out[9]=in->pad_side;
    put32(out+12,in->score);put32(out+16,in->hiscore);
    put32(out+20,(uint32_t)in->timer);put32(out+24,(uint32_t)in->health);
    out[28]=(uint8_t)(in->kills>>8);out[29]=(uint8_t)in->kills;
    put32(out+32,in->seed);
    /* 36..39: a checksum of the rest */
    uint32_t sum=0x811C9DC5u;
    for(unsigned i=0;i<36;++i)sum=(sum^out[i])*0x01000193u;
    put32(out+36,sum);
}

int t3_force_handoff_unpack(const uint8_t* in,uint32_t bytes,t3_force_handoff_t* out){
    if(bytes!=T3_FORCE_HANDOFF_BYTES||in[0]!='G'||in[1]!='F'||in[2]!=T3_FORCE_HANDOFF_VERSION)return 0;
    uint32_t sum=0x811C9DC5u;
    for(unsigned i=0;i<36;++i)sum=(sum^in[i])*0x01000193u;
    if(sum!=get32(in+36))return 0;
    if(in[3]>3||in[4]>T3_FORCE_SECTION_RESULT||in[7]>3||in[8]>15||in[9]>1)return 0;
    out->level=in[3];out->section=in[4];out->character=in[5];out->costume=in[6];out->keys=in[7];out->flags=in[8];
    out->pad_side=in[9];
    out->score=get32(in+12);out->hiscore=get32(in+16);
    out->timer=(int32_t)get32(in+20);out->health=(int32_t)get32(in+24);
    out->kills=(uint16_t)(in[28]<<8|in[29]);
    out->seed=get32(in+32);
    return 1;
}
