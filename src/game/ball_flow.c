/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of Tekken Ball's opponent choice as ported in the pinned Tekken3Remake
 * (tools/research/screens_sim.py ball_opponent). See ball_flow.h for the changes. */
#include "ball_flow.h"

typedef struct { uint8_t character; uint16_t met; } pair_t;

/* FUN_8004D068 with ascending = 0: a stable insertion sort of the pairs by the unsigned key, largest first. */
static void sort_pairs(pair_t* p,unsigned n) {
    unsigned i;
    for(i=0;i+1<n;++i) {
        int j=(int)i;
        while(j>=0) {
            const pair_t a=p[j],b=p[j+1];
            if(!(a.met<b.met))break;
            p[j]=b;p[j+1]=a;--j;
        }
    }
}

uint32_t t3_ball_opponent(t3_ball_choice_t* s) {
    pair_t pairs[BALL_CHARACTERS];
    unsigned n=0,count=0,c;
    const uint32_t mask=s->unlocked&BALL_OPPONENT_POOL;
    uint32_t key;
    for(c=0;c<BALL_CHARACTERS;++c)if(mask>>c&1u){pairs[n].character=(uint8_t)c;pairs[n].met=s->met[c];++n;}
    sort_pairs(pairs,n);
    while(count<n&&!((uint32_t)pairs[0].met+4u<pairs[count].met))++count;
    if(!s->first_done) {
        s->first_done=1;
        return BALL_GON_KEY;
    }
    if(count==0)return BALL_GON_KEY;
    s->lcg=s->lcg*5u+1u;
    c=pairs[s->lcg%count].character;
    key=c*4u+(s->frame&1u);
    ++s->met[c];
    return key;
}
