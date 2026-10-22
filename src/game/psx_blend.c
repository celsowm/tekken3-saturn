/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Adapted from pinned Tekken3Remake tools/research/fight_sim.py live transition
 * blend, RootUpdate and pose composition. Changes: explicit caller state and
 * sampling callback and explicit global hold; replay playback remains separate.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "sim_profile.h"
#include "psx_blend.h"
#include <saturn/int_math.h>
static int16_t wrap16(int32_t v) {
    uint16_t u=(uint16_t)v;return (int16_t)(u<32768u?(int32_t)u:(int32_t)u-65536);
}
static int32_t wrap32(int64_t v) {
    uint32_t u=(uint32_t)v;return u<0x80000000u?(int32_t)u:(int32_t)((int64_t)u-4294967296LL);
}
static int entry_dst(unsigned code) {
    switch(code){case 2:case 0x12:case 0x13:case 0x14:case 0x15:case 0x16:
    case 0x19:case 0x1A:case 0x1D:case 0x2C:return 1;default:return 0;}
}
int T3_SIM_IMPL(t3_psx_blend_prepare)(t3_psx_blend_t* s,const t3_psx_blend_facts_t* f,
                         t3_psx_blend_sample_t sample,void* user) {
    int n=0,forward=1,left;
    if(!s || !f || !sample || s->mode>3 || s->frames<0 || s->frames>32767)return 0;
    if(f->global_hold) {
        s->mode=0;
        if(!s->active)s->counter=s->frames=0;
        goto finish;
    }
    if(f->playback){n=s->frames;goto apply;}
    s->active=0;
    if(!f->suppressed) {
        if(s->mode==T3_BLEND_DECAY && !s->counter)s->mode=0;
        if((s->last_row!=f->row || f->branch_kind==3u) && f->changed) {
            left=(int)f->length-f->pose_frame;
            if(left>0) {
                n=(!f->active_first || f->active_first<=f->pose_frame)?
                    (f->throw_state<0?16:4):(int)f->active_first-f->pose_frame;
                if(n>16)n=16;
                if(n>left)n=left;
                if(n<2)n=0;
            }
        }
        if(n){s->active=1;s->mode=T3_BLEND_DECAY;}
        forward=s->mode!=T3_BLEND_DECAY;
    } else if(s->mode==T3_BLEND_DECAY)s->mode=0;
    if(forward) {
        if(!f->no_forward) {
            if((s->mode==T3_BLEND_BRANCH && s->counter==s->frames) ||
               (s->mode==T3_BLEND_STANCE && (s->counter==s->frames || f->changed)))s->mode=0;
            if(!s->mode) {
                n=(f->pending && f->pose_frame<f->entry_frame && f->branch_kind!=3u)?
                    f->entry_frame-f->pose_frame+1:0;
                s->mode=T3_BLEND_BRANCH;
                if(!n) {
                    left=(int)f->length-f->pose_frame;
                    if(!f->pending && left<16) {
                        n=f->frame_step>=1?left+1:f->pose_frame;
                        if(n<2)n=0;
                    }
                    s->mode=T3_BLEND_STANCE;
                }
                if(n)s->active=1;else s->mode=0;
            }
        } else if(s->mode==T3_BLEND_BRANCH || s->mode==T3_BLEND_STANCE)s->mode=0;
    }
apply:
    if(s->active) {
        int16_t aroot[3],broot[3];
        t3_psx_matrix_t a[18],b[18];
        s->frames=n;
        if(s->mode==T3_BLEND_DECAY) {
            s->counter=n-1;
            if(!f->playback) {
                s->src_row=s->last_row;s->dst_row=f->row;
                s->src_frame=(uint8_t)(f->previous_event_frame-1);
                s->dst_frame=(uint8_t)(f->pose_frame-1);
            }
            if(!sample(user,s->dst_row,s->dst_frame,broot,b))return 0;
            for(unsigned k=0;k<3;++k)s->root_delta[k]=wrap16(s->previous_root[k]-broot[k]);
            for(unsigned i=1;i<18;++i)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
                s->delta[i-1].m[r][c]=wrap16(s->previous[i-1].m[r][c]-b[i].m[r][c]);
        } else {
            s->counter=0;
            if(!f->playback) {
              s->src_row=f->row;
              if(s->mode==T3_BLEND_BRANCH) {
                s->src_frame=(uint8_t)f->entry_frame;s->dst_row=f->pending_row;
                s->dst_frame=(uint8_t)(entry_dst(f->transition)?f->entry_frame:0);
              } else {
                s->src_frame=f->frame_step<1?0:f->length;s->dst_row=f->stance_row;s->dst_frame=0;
              }
            }
            if(!sample(user,s->src_row,s->src_frame,aroot,a) ||
               !sample(user,s->dst_row,s->dst_frame,broot,b))return 0;
            for(unsigned k=0;k<3;++k)s->root_delta[k]=wrap16(broot[k]-aroot[k]);
            for(unsigned i=1;i<18;++i)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
                s->delta[i-1].m[r][c]=wrap16(b[i].m[r][c]-a[i].m[r][c]);
        }
    } else if(s->mode==T3_BLEND_DECAY) {
        if(s->counter>0)--s->counter;else if(s->counter<0)s->counter=0;
    } else if(!s->mode)s->counter=s->frames=0;
    else if(s->counter<s->frames)++s->counter;
    else if(s->counter>s->frames)s->counter=s->frames;
finish:
    s->last_row=f->row;s->last_frame=f->pose_frame;s->previous_root[1]=f->decoded_root_dy;
    return 1;
}
T3_SIM_WRAP(int,t3_psx_blend_prepare,T3_SIM_BLEND,
    (t3_psx_blend_t* s,const t3_psx_blend_facts_t* f,
                         t3_psx_blend_sample_t sample,void* user),
    (s,f,sample,user))

int16_t t3_psx_blend_root_y(t3_psx_blend_t* s,int16_t y) {
    if(!s->mode || !s->frames)s->weight=4096;
    else {
        int32_t v;
        s->weight=wrap16(wrap32((int64_t)s->counter*4096)/s->frames);
        v=wrap32((int64_t)s->root_delta[1]*s->weight*2);
        y=wrap16(sat_floor_shift_i64((int64_t)v+(v<0?8191:0),13)+y);
    }
    s->previous_root[1]=y;return y;
}
int T3_SIM_IMPL(t3_psx_blend_locals)(t3_psx_blend_t* s,const t3_psx_rsqrt_t* table,t3_psx_matrix_t slots[18]) {
    if(!s || !slots || s->mode>3)return 0;
    for(unsigned i=1;i<18;++i) {
        if(s->mode && (!t3_psx_matrix_add_scaled(&s->delta[i-1],&slots[i],s->weight,&slots[i]) ||
                      !t3_psx_matrix_orthonormalize(&slots[i],table,&slots[i])))return 0;
        t3_psx_matrix_copy(&s->previous[i-1],&slots[i]);
    }
    return 1;
}
T3_SIM_WRAP(int,t3_psx_blend_locals,T3_SIM_BLEND,
    (t3_psx_blend_t* s,const t3_psx_rsqrt_t* table,t3_psx_matrix_t slots[18]),
    (s,table,slots))
