/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of force.ovl FUN_800B598C as ported in the pinned Tekken3Remake force_sim.py
 * (choose_target); see psx_force_target.h. */
#include "psx_force_target.h"

/* FUN_80043C88: the pair distance of two fighter slots. */
static int32_t pair_distance(const t3_force_target_inputs_t* in,unsigned i,unsigned j){
    const unsigned x=i>1u?1u<<((i-1u)&31u):i,y=j>1u?1u<<((j-1u)&31u):j;
    const unsigned k=x+y;
    return k<5u?in->pair[k]:0;
}
static int32_t absdiff16(int16_t x,int16_t y){const int32_t d=(int32_t)x-y;return d<0?-d:d;}

int t3_force_choose_target(t3_force_target_t* s,const t3_force_target_inputs_t* in,
                           const t3_force_candidate_t* a,const t3_force_candidate_t* b){
    int pick=0;
    unsigned first=a->index;
    if(s->wait>0)--s->wait;
    if(s->wait<0)s->wait=0;
    if(a->hidden){
        if(!b->hidden)pick=1;
    }else if(b->hidden){
    }else if(a->close==0){
        if(b->close)pick=1;
    }else if(b->close==0||s->wait!=0){
    }else{
        const int32_t da=pair_distance(in,in->player_index,a->index),db=pair_distance(in,in->player_index,b->index);
        const int32_t diff=(int32_t)((uint32_t)da-(uint32_t)db);
        if(da<0x9C4||db<0x9C4){
            if((diff<0?-(int64_t)diff:(int64_t)diff)>=0x190)pick=diff>0;
            else if(a->quadrant==b->quadrant){
                const int32_t thr=a->quadrant==0?0x1000:a->quadrant==2?0x2000:0x3000;
                if(absdiff16(a->rel,b->rel)>=thr)pick=a->rel>=b->rel;
            }else{
                const int32_t oa=in->quadrant_order[a->quadrant&3],ob=in->quadrant_order[b->quadrant&3];
                pick=!(ob<oa);
            }
        }
        if(s->manual){
            uint32_t side=s->manual_side;
            if(in->pad_pressed&s->manual_button)side=(side+1u)&1u;
            s->manual_side=side;
            pick=s->manual_side!=0;
        }
        if((pick?b->index:a->index)!=first)s->wait=0x78;
    }
    if(s->manual)s->manual_side=(uint32_t)(pick!=0);
    return pick;
}
