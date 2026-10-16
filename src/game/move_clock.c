/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of MoveStartOrAdvance's clock/start paths from the pinned
 * Tekken3Remake tools/research/fight_sim.py; changes: explicit 16-bit wrapping,
 * value-owned state, argument validation and a root-move binding result.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "move_clock.h"

static int16_t add16(int16_t a,int16_t b) {
    const uint16_t bits=(uint16_t)((int32_t)a+b);
    return (int16_t)(bits<0x8000u?(int32_t)bits:(int32_t)bits-65536);
}
void t3_move_clock_reset(t3_move_clock_t* c,int16_t pose_frame) {
    if(!c)return;
    *c=(t3_move_clock_t){0};
    c->pose_frame=c->root_frame=pose_frame;
    c->move_frame=pose_frame;
    c->frame_step=1;
    c->hold_frames=-1;
}
int t3_move_clock_advance(t3_move_clock_t* c,uint8_t hold_frame) {
    t3_move_clock_t n;
    int bind=0;
    if(!c || c->ballistic>1u || c->move_changed>1u)return -1;
    n=*c;
    n.move_frame=add16(n.move_frame,1);
    n.move_changed=0u;
    if(n.hit_freeze>0) {
        --n.hit_freeze;
        if(n.hit_freeze==0) {
            n.pose_frame=add16(n.pose_frame,1);
            n.root_frame=n.pose_frame;
            bind=1;
        }
    } else if(n.hold_frames<0) {
        if(!n.ballistic) {
            n.pose_frame=add16(n.pose_frame,n.frame_step);
            if(n.pose_frame<1)n.pose_frame=1;
            n.root_frame=n.pose_frame;
        } else if((uint16_t)n.pose_frame!=hold_frame) {
            n.pose_frame=add16(n.pose_frame,1);
            n.root_frame=n.pose_frame;
        }
    } else {
        if((uint16_t)n.pose_frame==hold_frame) {
            if(n.hold_frames==0) {
                n.pose_frame=add16(n.pose_frame,1);
                n.root_frame=n.pose_frame;
                bind=1;
            } else {
                --n.hold_frames;
                n.root_frame=add16(n.root_frame,1);
            }
        } else {
            n.pose_frame=add16(n.pose_frame,1);
            n.root_frame=add16(n.root_frame,1);
        }
    }
    *c=n;return bind;
}

int t3_move_clock_pending_ready(const t3_move_clock_t* c,int16_t entry,uint8_t length) {
    if(!c || !length)return -1;
    if(c->frame_step<0)return c->pose_frame<=entry || c->pose_frame<1;
    if(c->frame_step==0)return 1;
    return c->pose_frame>=entry || length<=c->pose_frame;
}
int t3_move_clock_begin(t3_move_clock_t* c,const t3_move_start_t* s) {
    t3_move_clock_t n;
    unsigned code;
    int bind=1,keep;
    if(!c || !s || !s->next_length || !s->current_length || !s->root_length ||
       s->branch_kind>4u || s->same_animation>1u || s->got_hit>1u || s->alive>1u ||
       c->ballistic>1u || c->move_changed>1u)return -1;
    code=s->transition;
    if(!(code<=29u || code==35u || (code>=37u && code<=43u)))return -1;
    n=*c;n.hold_frames=-1;n.frame_step=1;n.hit_freeze=0;
    if(!s->got_hit)n.ballistic=0;
    if(s->got_hit && s->alive)n.hit_freeze=s->hit_freeze_in;
    if(s->branch_kind==1u || s->branch_kind==3u || !s->same_animation)
        n.move_changed=1u;
    if(code==1u || code==2u || code==3u || code==18u || code==19u ||
       code==20u || code==21u || code==22u || code==25u || code==29u) {
        keep=s->branch_kind!=3u && (!s->next_active_first ||
             (s->next_active_first<=s->next_length && n.pose_frame<=s->next_active_last));
        if(keep) {
            n.pose_frame=add16(n.pose_frame,1);n.root_frame=add16(n.root_frame,1);
        } else n.pose_frame=n.root_frame=1;
        if(n.pose_frame>s->next_length)n.pose_frame=n.root_frame=s->next_length;
    } else if(code==4u || code==5u) {
        n.pose_frame=1;n.root_frame=add16(n.root_frame,1);
        if(n.root_frame>s->root_length)n.root_frame=s->root_length;
        n.hold_frames=s->branch_window;bind=0;
    } else if(code==7u) {
        n.pose_frame=n.root_frame=s->next_length;n.frame_step=-1;
    } else if(code==8u) {
        n.pose_frame=add16(n.pose_frame,-1);
        if(n.pose_frame<1)n.pose_frame=1;
        n.root_frame=n.pose_frame;n.frame_step=-1;
    } else if(code==17u) {
        bind=0;
    } else if(code==26u || code==27u) {
        n.pose_frame=add16(n.pose_frame,1);n.root_frame=add16(n.root_frame,1);
        if(n.pose_frame>s->next_length)n.pose_frame=n.root_frame=s->next_length;
    } else n.pose_frame=n.root_frame=1;
    n.move_frame=n.move_changed?1:n.pose_frame;
    *c=n;return bind;
}
