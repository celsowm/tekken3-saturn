/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from pinned Tekken3Remake fighter_hands.gd and move_events.gd's
 * documented HandFaceCommand (0x800346F8) and FighterHandPoses (0x80034BC4)
 * hand channels (docs/research/formats/3dmk-models.md, vertex variants).
 * Changes: compact C state; the face shapes and True Ogre's level are kept
 * elsewhere. NOTICE.md. */
#include "psx_hands.h"

enum { FIST=0x200, HAND_FIRST=14u, HAND_LAST=61u, HAND_RESET=20u, HAND_COSTUME=21u };

static int16_t s16(int32_t v){return (int16_t)(uint16_t)(uint32_t)v;}
static int div_trunc(int a,int b){return a/b;}

void t3_psx_hands_command(t3_psx_hands_t* h,unsigned which,unsigned shape,int speed) {
    if(shape>=8u)return;
    for(unsigned c=0;c<2u;++c) {
        if(!(which&(c+1u)))continue;
        const int value=shape==0u?0:shape==1u?FIST:(int)shape+FIST-1;
        if(h->current[c]<=FIST && value<=FIST) {
            if(speed)h->rate[c]=s16(speed<0?div_trunc(FIST,1-speed):div_trunc(FIST,speed+1));
        } else {
            h->current[c]=(int16_t)value;
            h->rate[c]=0;
        }
        h->target[c]=(int16_t)value;
    }
}

int t3_psx_hands_event(t3_psx_hands_t* h,uint16_t event,unsigned costume_shape) {
    const unsigned hi=(unsigned)event>>8u,lo=event&0xFFu;
    if(hi<HAND_FIRST || hi>HAND_LAST)return 0;
    const unsigned cmd=hi-HAND_FIRST;
    unsigned which=cmd>>4u;
    if(!which)which=3u;
    unsigned shape=cmd&7u;
    int speed=(int8_t)lo;
    if(hi==HAND_RESET){shape=0u;speed=(int)lo;}
    else if(hi==HAND_COSTUME){shape=costume_shape;speed=(int)lo;}
    t3_psx_hands_command(h,which,shape,speed);
    return 1;
}

static uint8_t variant_of(int value) {
    if(value>FIST)return (uint8_t)(value-(FIST-3));
    const int v=value>>7;
    return (uint8_t)(v?v-1:0);
}

void t3_psx_hands_step(t3_psx_hands_t* h) {
    for(unsigned c=0;c<2u;++c) {
        if(h->current[c]<h->target[c]) {
            h->current[c]=s16(h->current[c]+h->rate[c]);
            if(!(h->current[c]<h->target[c])){h->rate[c]=0;h->current[c]=h->target[c];}
        } else if(h->target[c]<h->current[c]) {
            h->current[c]=s16(h->current[c]-h->rate[c]);
            if(!(h->target[c]<h->current[c])){h->rate[c]=0;h->current[c]=h->target[c];}
        }
        h->variant[c]=variant_of(h->current[c]);
    }
}
