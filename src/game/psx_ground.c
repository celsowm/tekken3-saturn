/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of integer angles/polar, grounded FighterMovePhysics,
 * _tracking and _guard_state in pinned Tekken3Remake research.
 * Changes: portable typed state, bounded tables, explicit wrap, atomic writes.
 * Throw starts (33/34) follow VS rules; mode-specific behavior is not
 * approximated.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "sim_profile.h"
#include "psx_ground.h"
#include <saturn/int_math.h>

static int16_t s16(int32_t n) {
    uint16_t u=(uint16_t)n;return (int16_t)(u<32768u?(int32_t)u:(int32_t)u-65536);
}
static int32_t w32(int64_t n) {
    uint32_t u=(uint32_t)(uint64_t)n;
    return u<0x80000000u?(int32_t)u:(int32_t)((int64_t)u-4294967296LL);
}
static int16_t le16(const uint8_t* p) {return s16((int32_t)(p[0]|((uint16_t)p[1]<<8)));}
static int32_t abs16(int32_t n) {int32_t v=s16(n);return v<0?-v:v;}
static int32_t floor256(int32_t n) {return sat_floor_shift_i32(n,8u);}
int t3_psx_ground_tables_bind(t3_psx_trig_t trig,const uint8_t* blob,size_t size,
                              t3_psx_ground_tables_t* out) {
    if(!blob || !out || size!=T3_PSX_GROUND_BLOB_BYTES)return 0;
    *out=(t3_psx_ground_tables_t){trig,blob,T3_PSX_ATAN_BYTES,blob+T3_PSX_ATAN_BYTES,
        T3_PSX_PUSH_BYTES+T3_PSX_LAUNCH_PUSH_BYTES,
        blob+T3_PSX_ATAN_BYTES+T3_PSX_PUSH_BYTES+T3_PSX_LAUNCH_PUSH_BYTES};
    return 1;
}
/* ISqrt (0x8002A578): Newton iteration from a power-of-two estimate. */
static uint32_t isqrt(uint32_t n) {
    uint64_t guess=1,rest=n,prev;
    if(!n)return 0;
    while(guess<rest){guess<<=1;rest>>=1;}
    for(;;){prev=guess;guess=(n/prev+prev)>>1;if(guess>=prev)return (uint32_t)prev;}
}
/* LaunchTrajectory (0x8002F2C8), after fight_math.launch_trajectory: the
 * flight speed, rise and the entry frame that makes the fall end on the
 * ground. The sum is divided as an unsigned int and later steps wrap at 32
 * bits, as in the original (high bounces depend on it). */
static int launch_trajectory(t3_psx_ground_state_t* n,const t3_move_record_t* m,
                             const t3_move_record_t* previous,int16_t pose_frame) {
    const int32_t hold=m->hold_frame?m->hold_frame:previous->length;
    const int32_t y=n->root.y,ground=n->ground_offset,dmg=n->last_damage,jug=n->juggle_count;
    const int same=previous->animation_offset==m->animation_offset;
    int32_t speed=dmg+(jug+1)*7+10+(n->air_kind==5u?10:0),v,vc,sq,e,entry,fall,count;
    uint32_t t;
    n->air_speed=(int16_t)(speed<0?0:speed>100?100:speed);
    v=w32((int64_t)dmg*4-sat_floor_shift_i32(w32((int64_t)n->velocity.y+n->hit_dir.y),3u)-(jug*7-40));
    if(v< -100){vc=-100;sq=10000;}
    else {vc=v<100?v:100;sq=vc*vc;}
    n->air_vel_y=s16(-vc);
    {
        int32_t r=w32((int64_t)sq+(int64_t)w32((int64_t)ground+y)*12);
        uint32_t mag=r<0?(uint32_t)0-(uint32_t)r:(uint32_t)r;
        t=((uint32_t)vc+isqrt(mag))/6u;
    }
    e=w32(-(int64_t)t);
    if(!t){t=1;e=-1;}
    e=w32((int64_t)hold+e+1);
    if(e<1)e=1;
    if(!(n->state&4u) && e!=1){t=t-1u+(uint32_t)e;entry=1;}
    else if(!jug) {
        t+=10u;entry=w32((int64_t)hold-(int64_t)t+1);
        if(entry<1)entry=1;
    } else {
        int32_t back=0;
        entry=e;
        if(same && (pose_frame-e<0?e-pose_frame:pose_frame-e)<10) {
            entry=pose_frame-10;back=-entry;
            if(entry<1){entry=1;back=-1;}
            t=t+(uint32_t)e+(uint32_t)back;
        }
    }
    count=w32((int64_t)t+1);
    if(!count)return 0;
    {
        const uint64_t c=(uint64_t)(int64_t)count;
        fall=sat_floor_shift_i32(w32((int64_t)(uint32_t)(c*c*6u)),1u);
    }
    if(w32((int64_t)w32((int64_t)vc*count-fall)-y)<ground) {
        const int64_t num=-(int64_t)w32((int64_t)ground+y+fall);
        n->air_vel_y=s16((int32_t)(num/count));
    }
    n->push_cursor=T3_PSX_PUSH_BYTES;n->push_table_frames=8;
    n->launch_entry=s16(entry);
    n->push_direction=s16(n->target_dir-0x8000);
    if(n->air_kind==1u) {
        n->air_vel_y=400;n->launch_entry=6;
        n->push_frames=m->length>6u?(int16_t)(m->length-6u):0;n->push_speed=0x28;
    }
    return 1;
}
static int valid(const t3_psx_ground_tables_t* t) {
    return t && t->trig.data && t->trig.size==T3_PSX_TRIG_BYTES && t->atan && t->atan_size==1025u;
}
static int16_t trig(const t3_psx_ground_tables_t* t,uint16_t angle,unsigned cosine) {
    int32_t v=le16(t->trig.data+((angle>>3)&0x1FFEu)+(cosine?0x800u:0u));
    if(v>4095)v=4095;else if(v< -4095)v=-4095;
    return (int16_t)(v*8);
}
static uint16_t atan_value(const t3_psx_ground_tables_t* t,uint32_t x) {
    unsigned i=x/4u;return (uint16_t)(t->atan[i]+(i>>1));
}
int t3_psx_atan2(const t3_psx_ground_tables_t* t,int32_t dz,int32_t dx,uint16_t* out) {
    int32_t z=s16(dz),x=s16(dx);
    uint32_t a=(uint32_t)abs16(z),b=(uint32_t)abs16(x),s;
    uint16_t u,r;
    if(!valid(t) || !out)return 0;
    if(a==b)s=0x200u;
    else if(b<a)s=atan_value(t,(b<<12)/a);
    else s=0x400u-atan_value(t,(a<<12)/b);
    u=(uint16_t)(s*16u);
    if(z<0)r=x<0?(uint16_t)(u+0x8000u):(uint16_t)(0x7FFFu-u);
    else r=x<0?(uint16_t)~u:u;
    *out=r;return 1;
}
int t3_psx_polar(const t3_psx_ground_tables_t* t,int32_t n,uint16_t angle,int32_t* dx,int32_t* dz) {
    int32_t x,z;
    if(!valid(t) || !dx || !dz)return 0;
    x=w32((int64_t)n*trig(t,angle,0))/32767;
    z=w32((int64_t)n*trig(t,angle,1))/32767;
    *dx=x;*dz=z;return 1;
}
static uint8_t quadrant(uint16_t delta) {
    const uint16_t q=(uint16_t)(delta+0x2000u);
    return q<0x4000u?0u:q<0x8000u?1u:q<0xC000u?2u:3u;
}
static uint16_t folded(uint16_t angle) {return angle&0x8000u?(uint16_t)~angle:angle;}
int t3_psx_ground_angles(t3_psx_ground_state_t* state,const t3_psx_ground_state_t* opp,
                         const t3_psx_ground_tables_t* t) {
    t3_psx_ground_state_t n;
    if(!state || !opp || !valid(t))return 0;
    n=*state;
    if(!n.fixed_facing) {
        if(!t3_psx_atan2(t,w32((int64_t)opp->root.z-n.root.z),
                         w32((int64_t)opp->root.x-n.root.x),&n.target_dir))return 0;
        n.opp_to_self_dir=(uint16_t)(n.target_dir-0x8000u);n.opp_heading=opp->heading;
    } else {
        n.target_dir=n.fixed_facing_side?(uint16_t)-0x4000:0x4000u;
        n.opp_to_self_dir=n.opp_heading=(uint16_t)(n.target_dir-0x8000u);
    }
    n.heading_delta=(uint16_t)(n.heading-n.target_dir);
    n.opp_heading_delta=(uint16_t)(n.opp_heading-n.opp_to_self_dir);
    n.facing_quadrant=quadrant(n.heading_delta);n.opponent_quadrant=quadrant(n.opp_heading_delta);
    n.relative_angle=folded(n.heading_delta);n.opp_relative_angle=folded(n.opp_heading_delta);
    if(n.distance>512u) {
        n.aim_dir=n.relative_angle<0x4000u?n.target_dir:(uint16_t)(n.target_dir-0x8000u);
        n.opp_aim_dir=n.opp_relative_angle<0x4000u?n.opp_to_self_dir:(uint16_t)(n.opp_to_self_dir-0x8000u);
    }
    *state=n;return 1;
}
static int16_t clamp(int32_t n,int32_t limit) {
    int32_t v=s16(n);
    if(v<0)return s16(v< -limit?-limit:v);
    return s16(v>limit?limit:v);
}
static int16_t towards(uint16_t target,uint16_t heading,int frames) {
    return s16(s16((int32_t)target-heading)/(frames?frames:1));
}
static void timed_turn(t3_psx_ground_state_t* n,const t3_move_vm_t* vm) {
    n->turn_frames=s16(vm->move.active_first-vm->clock.pose_frame);
    if(n->turn_frames<1)n->turn_frames=1;
    n->turn_step=clamp(towards(n->target_dir,n->heading,n->turn_frames),0x9F4);
    n->track_mode=12;
}
static void track_step(t3_psx_ground_state_t* n,int16_t step,int budget,int facing,unsigned frame,unsigned first) {
    int rest=budget-abs16(n->track_accum);
    int16_t over=0;
    if(budget<abs16(n->track_accum)+abs16(step))over=s16(step<0?-s16(rest):s16(rest));
    if(over){n->track_mode=6;step=over;}
    n->heading=(uint16_t)(n->heading+step);if(facing)n->facing=n->heading;
    n->track_accum=s16(n->track_accum+step);if(first<=frame)n->track_mode=6;
}
static void tracking(t3_psx_ground_state_t* n,const t3_psx_ground_state_t* opp,
                       const t3_move_vm_t* vm,const t3_psx_ground_tables_t* t) {
    const int pose=vm->clock.pose_frame,age=vm->clock.move_frame;
    const unsigned first=vm->move.active_first,last=vm->move.active_last;
    int remain=(int)first+1-pose,alert=opp->attack_alert!=0 && n->attack_segment_count!=0;
    int degrees,limit,delta;
    if(remain<1)remain=1;
    switch(n->track_mode) {
    case 0:n->heading=n->facing;break;
    case 1:track_step(n,clamp(towards(n->aim_dir,n->heading,remain),0x1555),0x7FFF,1,(unsigned)pose,first);break;
    case 2:case 3:
        if(alert && age<9){timed_turn(n,vm);break;}
        degrees=n->in_air?1:age>7?14:3;limit=degrees*65535/360;
        if(n->track_mode==3)limit=s16(limit/2);
        track_step(n,clamp(towards(n->target_dir,n->heading,remain),limit),0x5555,0,(unsigned)pose,first);break;
    case 4:
        if(!n->turn_frames && !alert) {
            degrees=n->in_air?1:age>7?3:2;
            track_step(n,clamp(towards(n->target_dir,n->heading,remain),degrees*65535/360),0xE38,0,(unsigned)pose,first);
        }break;
    case 5:
        if(!alert || age>8) {
            degrees=n->in_air?4:age>7?100:3;
            track_step(n,clamp(towards(n->target_dir,n->heading,remain),s16(degrees*65535/360)),0x9554,0,(unsigned)pose,first);
        } else timed_turn(n,vm);
        break;
    case 6:
        if(!n->damage || !vm->move.air_start || n->in_air || pose<(int)last) {
            if((int)first<pose && (pose-(int)first)%5==0 && n->relative_angle<0x5556u) {
                n->turn_frames=s16((int)vm->move.length-pose<6?5:(int)vm->move.length-pose);
                if(n->turn_frames<0)n->turn_frames=0;
                else if(n->turn_frames)n->turn_step=clamp(towards(n->aim_dir,n->heading,n->turn_frames),0x222);
            }
        } else n->turn_frames=n->turn_step=0;
        break;
    case 7:
        limit=vm->move.air_end?vm->move.air_end:vm->move.length;
        if(pose<=limit) {
            delta=s16(s16((int32_t)n->aim_dir-n->heading)*pose/(limit?limit:1));
            n->heading=(uint16_t)(n->heading+clamp(delta,0x38E));
        }break;
    case 8:
        limit=age<8?age:8;delta=s16(s16((int32_t)n->aim_dir-n->heading)*limit/8);
        n->heading=(uint16_t)(n->heading+clamp(delta,0x71C));break;
    case 9:
        if(n->distance>0x4AFu) {
            uint16_t aim;
            uint32_t diff;
            t3_psx_atan2(t,w32((int64_t)n->opponent_start.z-n->root.z),w32((int64_t)n->opponent_start.x-n->root.x),&aim);
            diff=(uint32_t)n->facing-aim;if(diff&0x8000u)diff=~diff;
            if((diff&65535u)>0x3FFFu)aim=(uint16_t)(aim-0x8000u);
            delta=s16(s16((int32_t)aim-n->facing)*pose/vm->move.length);
            n->facing=(uint16_t)(n->facing+clamp(delta,0x5B0));n->anchor_dirty=1;n->heading=n->facing;
        }break;
    case 10:
        if(pose<=vm->move.length) {
            /* Xiaoyu steps less from the back quadrant. Rule from Tekken3Remake
             * tools/research/fight_sim.py; PolyForm Noncommercial, NOTICE.md. */
            if(n->character==7u && n->facing_quadrant==2u)delta=n->root_dx<0?0x71C:-0x71C;
            else delta=n->root_dx<0?-0x11C7:0x11C7;
            n->anchor_dirty=1;n->facing=(uint16_t)(n->aim_dir+s16(delta*pose/vm->move.length));n->heading=n->facing;
        }break;
    default:break;
    }
}
static int load_push(t3_psx_ground_state_t* n,const t3_psx_ground_tables_t* t,int16_t index) {
    size_t offset;
    if(index<0)return 0;
    offset=(size_t)(uint16_t)index*2u;
    if(!t->push || offset+20u>t->push_size)return 0;
    n->push_frames=le16(t->push+offset);n->push_speed=le16(t->push+offset+2u);
    n->push_cursor=(uint16_t)(offset+4u);n->push_table_frames=8;
    return 1;
}
int T3_SIM_IMPL(t3_psx_ground_start)(t3_psx_ground_state_t* state,const t3_psx_ground_state_t* opp,
                        const t3_move_vm_t* vm,const t3_move_record_t* previous,
                        const t3_psx_ground_tables_t* t,uint8_t flags,uint8_t end_turn,
                        t3_move_start_facts_t facts,const t3_psx_reaction_pick_t* reaction,uint8_t segments) {
    t3_psx_ground_state_t n;
    const t3_move_record_t* m;
    unsigned code,reanchor;
    int tail=0,anchor=0,clear_turn=0;
    if(!state || !opp || !vm || !previous || !valid(t) || !vm->move.length ||
       facts.got_hit>1u || facts.alive>1u || end_turn>1u || segments>2u)return 0;
    code=vm->current_transition;
    if(!(code<=30u || code==33u || code==34u || code==35u || code==36u || (code>=37u && code<=43u)))return 0;
    n=*state;m=&vm->move;
    n.in_reaction=n.hit_clean;
    n.air_phase=n.slide_state=n.track_mode=n.step_kind=0u;n.track_accum=0;n.hit_cooldown=0;
    n.opponent_start=opp->root;
    n.damage=n.damage_override?n.damage_override:((m->damage_class>>14)==1u?0:(int16_t)(m->damage_class&0x3FFFu));
    n.state=m->state_word;n.state_class=(uint8_t)((n.state>>11)&31u);
    n.human_guard=n.is_cpu?0u:1u;
    if((n.state&0x100u) && !n.human_guard)n.state&=~0x18u;
    n.guard=(uint16_t)(n.state&0x18u);n.attack_segment_count=segments;
    if(!facts.got_hit){n.was_hit_this_move=0u;n.juggle_count=0;}
    if(previous->flags&0x10000u)n.power_timer=120;
    if(m->flags&0x800u)n.attack_alert=9;
    if(flags&128u){n.heading=(uint16_t)(n.heading-0x8000u);n.facing=(uint16_t)(n.facing-0x8000u);}
    if(end_turn && code!=34u)n.heading=n.facing=(uint16_t)(n.heading+previous->end_turn);
    if(!t3_psx_ground_angles(&n,opp,t))return 0;
    reanchor=(previous->flags>>28)&1u;
    switch(code) {
    case 0:n.heading=n.facing=n.target_dir;anchor=1;break;
    case 1:case 2:case 3:
        if(code==1u)n.facing=n.heading;
        n.track_mode=11;n.anchor_dirty=1u;anchor=1;break;
    case 4:
        n.track_mode=11;n.facing=n.heading;
        if(n.relative_angle<0x4FA5u) {
            n.turn_frames=m->active_first>1u?(int16_t)(m->active_first-1u):1;
            n.turn_step=towards(n.target_dir,n.heading,n.turn_frames);
        }break;
    case 5:n.facing=n.heading;n.track_mode=n.relative_angle<0x4FA4u?4u:6u;clear_turn=1;break;
    case 6:case 7:case 8:
        n.facing=n.heading;anchor=(int)reanchor;
        if(code==8u){n.anchor_dirty=1u;anchor=1;}break;
    case 9:n.track_mode=5;n.facing=n.aim_dir;clear_turn=1;tail=1;break;
    case 10:case 11:
        n.facing=code==10u?n.heading:n.aim_dir;
        n.turn_frames=m->active_first?(int16_t)(m->active_first-1u):
                      previous->length<16u?previous->length:16;
        if(n.turn_frames<1)n.turn_frames=1;
        n.aim_dir=n.relative_angle<0x4000u?n.target_dir:(uint16_t)(n.target_dir-0x8000u);
        n.turn_step=towards(n.aim_dir,n.heading,n.turn_frames);
        n.track_mode=11;anchor=(int)reanchor;break;
    case 12:case 13:case 14:
        if(n.relative_angle<0x4FA4u){n.facing=n.target_dir;n.track_mode=code==12u?4u:code==13u?3u:2u;}
        else {n.facing=n.aim_dir;n.track_mode=6u;}
        clear_turn=1;tail=1;break;
    case 15:n.track_mode=1;n.facing=n.aim_dir;clear_turn=1;tail=1;break;
    case 16:case 18:case 19:
        /* Slides close in on the opponent; the distance is taken when the
         * root move's airborne window opens (FighterMovePhysics). */
        n.track_mode=11;n.facing=n.target_dir;n.slide_state=1u;
        n.slide_step_x=n.slide_step_z=0;
        if(code==19u)n.heading=(uint16_t)(n.heading-0x8000u);
        n.turn_frames=m->active_first>1u?(int16_t)(m->active_first-1u):1;
        n.turn_step=towards(n.target_dir,n.heading,n.turn_frames);tail=1;break;
    case 17:break;
    case 30:case 36:
        /* Launch (air kind 5) and juggle: a ballistic flight from the entry
         * frame LaunchTrajectory picks; the caller moves the clock there. */
        if(!t->air_kinds || t->push_size<T3_PSX_PUSH_BYTES+T3_PSX_LAUNCH_PUSH_BYTES)return 0;
        if(code==30u){n.air_kind=5u;n.launch_armed=1u;n.ground_offset=le16(t->air_kinds+5u*4u);}
        else if(!n.juggle_count)n.air_vel_x=n.air_vel_y=n.air_vel_z=0;
        n.anchor.y=n.root.y;
        if(!launch_trajectory(&n,m,previous,vm->clock.pose_frame))return 0;
        n.air_phase=1u;n.juggle_count=s16(n.juggle_count+1);
        {
            int value=previous->flags&0x1000u?0:n.last_damage+n.recover_mash/2;
            n.recover_mash=s16(value)>65?66:s16(value);n.recover_frames=m->length;
        }
        n.track_mode=11;
        n.facing=(uint16_t)(n.aim_dir+((previous->flags&0x1000u) && reaction?reaction->push_angle:0));
        clear_turn=1;tail=1;break;
    case 20:case 21:
        n.track_mode=n.relative_angle<0x4FA4u?(code==20u?4u:2u):6u;
        n.anchor_dirty=1u;anchor=1;clear_turn=1;break;
    case 22:case 25:
        n.track_mode=8;n.anchor_dirty=1u;anchor=1;clear_turn=code==22u;break;
    case 23:n.track_mode=9;n.facing=n.heading;anchor=(int)reanchor;break;
    case 24:n.track_mode=8;n.facing=n.aim_dir;tail=1;break;
    case 26:case 27:n.facing=n.heading;n.step_kind=code==26u?1u:2u;break;
    case 28:n.track_mode=10;n.throw_state=0;n.facing=n.heading;clear_turn=1;
        anchor=(int)reanchor;n.step_cooldown=0x18;break;
    case 29:n.track_mode=10;n.throw_state=0;n.facing=n.heading;clear_turn=1;tail=1;break;
    case 33:
        /* Thrower: turned to the victim by its quadrant, then over 16 frames
         * to the victim's heading; anchored at the victim's root. Later
         * parts (or a reversed throw) take the partner's facing. */
        if(!n.throw_state) {
            static const uint16_t turn[4]={0x8000u,0xC000u,0u,0x4000u};
            n.heading=n.facing=(uint16_t)(n.target_dir+turn[n.opponent_quadrant&3u]);
            n.turn_frames=16;
            n.turn_step=s16(s16((int32_t)opp->heading-n.heading)/16);
            n.anchor.x=opp->root.x;n.anchor.z=opp->root.z;
        } else if(n.throw_state<0 || (m->flags&0x80000u)) {
            n.heading=n.facing=opp->facing;
            n.anchor.x=opp->root.x;n.anchor.z=opp->root.z;
        }
        n.push_frames=n.push_speed=n.push_table_frames=0;
        n.throw_state=n.throw_state<0?1:s16(n.throw_state+1);
        break;
    case 34:
        /* Victim: turned away from its own heading by 180 degrees and
         * anchored at its own root; the caller sets the down time from the
         * thrower's move. */
        if(n.throw_state>=0) {
            /* Counter throw: the victim was still in its attack startup, or
             * had power, when the throw started (transition 0x22, +0xBD). */
            const int counter=!(((previous->damage_word==0 ||
                (int16_t)previous->active_first<=facts.previous_pose_frame) &&
                opp->power_timer==0));
            n.attack_pending=counter?1u:0u;
            n.heading=n.facing=(uint16_t)(n.heading-0x8000u);anchor=1;
        } else if(m->flags&0x80000u){n.facing=n.heading;anchor=1;}
        n.push_frames=n.push_speed=n.push_table_frames=0;
        n.throw_state=n.throw_state<1?s16(n.throw_state-1):-1;
        break;
    default:
        if(!reaction || !reaction->selected || reaction->transition!=code ||
           !load_push(&n,t,reaction->push_index))return 0;
        if(code!=35u) {
            int value=previous->flags&0x1000u?0:n.last_damage+n.recover_mash/2;
            n.recover_mash=s16(value)>65?66:s16(value);n.recover_frames=m->length;
        }
        if(code==35u || code==38u) {n.track_mode=7;n.facing=n.aim_dir;}
        else {n.track_mode=11;n.facing=n.heading;}
        n.push_direction=s16((code==42u?n.aim_dir:n.target_dir)+reaction->push_angle-0x8000);
        if(code==37u)n.recover_mash=0;
        if(code==39u || code==40u || code==43u) {
            if(!n.push_repeat_timer)n.push_repeat=0;
            else if((uint16_t)n.push_repeat_transition==code)n.push_repeat=s16(n.push_repeat+1);
            n.push_repeat_transition=(int16_t)code;n.push_repeat_timer=(int16_t)(m->length+10u);
        }
        clear_turn=1;tail=1;break;
    }
    if(clear_turn)n.turn_frames=n.turn_step=0;
    if(anchor || (tail && reanchor)) {n.anchor.x=n.root.x;n.anchor.z=n.root.z;}
    if(code!=17u && code!=33u && code!=34u)n.throw_state=0;
    n.in_throw=n.throw_state!=0;
    /* Gon's moves start on the ground unless they begin with a step. */
    if(vm->bank && vm->bank->bank_type==0x13u && vm->clock.pose_frame==1 && m->air_start!=1u)n.anchor.y=0;
    *state=n;return 1;
}
T3_SIM_WRAP(int,t3_psx_ground_start,T3_SIM_GROUND,
    (t3_psx_ground_state_t* state,const t3_psx_ground_state_t* opp,
                        const t3_move_vm_t* vm,const t3_move_record_t* previous,
                        const t3_psx_ground_tables_t* t,uint8_t flags,uint8_t end_turn,
                        t3_move_start_facts_t facts,const t3_psx_reaction_pick_t* reaction,uint8_t segments),
    (state,opp,vm,previous,t,flags,end_turn,facts,reaction,segments))

int T3_SIM_IMPL(t3_psx_ground_step)(t3_psx_ground_state_t* state,const t3_psx_ground_state_t* opp,
                       const t3_move_vm_t* vm,const t3_move_vm_t* opponent_vm,
                       const t3_psx_ground_tables_t* t,uint8_t direction,uint8_t pressed) {
    t3_psx_ground_state_t n;
    const t3_move_record_t* m;
    int32_t dx,dz,push=0;
    int pose;
    if(!state || !opp || !vm || !opponent_vm || !valid(t) || !vm->move.length ||
       direction>9u || pressed>15u ||
       state->track_mode>12u || state->slide_state>2u || state->slide_to_point)return 0;
    n=*state;m=&vm->move;pose=vm->clock.pose_frame;
    n.landed_a=n.landed_b=n.about_to_hit=0;n.flight_landed=n.landing_shake=0;
    if(!t3_psx_ground_angles(&n,opp,t))return 0;
    if(n.slide_state==1u) {
        if(vm->root_move.air_start<=pose) {
            int frames=vm->root_move.air_end-pose;
            int32_t total=n.distance>0xC00u?0xA0C:w32((int64_t)n.distance-500);
            if(!frames)frames=1;
            if(!t3_psx_polar(t,s16(total/frames),n.target_dir,&n.slide_step_x,&n.slide_step_z))return 0;
            n.slide_state=2u;n.air_phase=2u;
        }
    } else if(n.slide_state==2u) {
        n.anchor.z=w32((int64_t)n.anchor.z+n.slide_step_z);
        n.anchor.x=w32((int64_t)n.anchor.x+n.slide_step_x);
        if(vm->root_move.air_end<=pose) {
            n.slide_state=0u;n.slide_step_x=n.slide_step_z=0;
            n.anchor.x=n.root.x;n.anchor.z=n.root.z;
        }
    }
    if(vm->clock.ballistic && !(vm->clock.hit_freeze && opponent_vm->bank && opponent_vm->bank->bank_type==0xEu)) {
        /* Ballistic flight: gravity 6 per frame, the horizontal speed along
         * the push-back direction, landing once falling past the ground offset. */
        int32_t vx,vz;
        n.air_vel_y=s16(n.air_vel_y+6);
        n.anchor.y=w32((int64_t)n.anchor.y+n.air_vel_y);
        n.anchor.x=w32((int64_t)n.anchor.x+n.air_vel_x);
        n.anchor.z=w32((int64_t)n.anchor.z+n.air_vel_z);
        if(!t3_psx_polar(t,n.air_speed,(uint16_t)(n.target_dir+0x8000u),&vx,&vz))return 0;
        n.air_vel_x=s16(vx);n.air_vel_z=s16(vz);
        if(n.air_vel_y>0 && -(int32_t)n.ground_offset<=n.anchor.y) {
            if(n.air_kind==1u)n.landing_shake=1u;
            n.anchor=(t3_vec3i_t){n.root.x,0,n.root.z};
            if(n.air_kind==5u)n.landed_a=1u;else n.landed_b=1u;
            n.flight_landed=1u;n.air_phase=2u;
        }
    } else if(m->air_start && pose>=(int)m->air_start-1 && pose<=m->air_end && m->step) {
        if(!t3_psx_polar(t,m->step,n.facing,&dx,&dz))return 0;
        n.step_accum_x=w32((int64_t)n.step_accum_x+dx);n.step_accum_z=w32((int64_t)n.step_accum_z+dz);
        dx=-floor256(n.step_accum_x);dz=-floor256(n.step_accum_z);
        n.anchor.x=w32((int64_t)n.anchor.x+dx);n.anchor.z=w32((int64_t)n.anchor.z+dz);
        n.step_accum_x=w32((int64_t)n.step_accum_x+dx*256LL);
        n.step_accum_z=w32((int64_t)n.step_accum_z+dz*256LL);
    }
    /* Gon is not thrown far while being hit, unless he is being juggled. */
    if(n.character==17u && n.in_reaction && !n.juggle_count) {
        if(!n.in_air)n.anchor.y=0;
        else {
            const int32_t y=w32((int64_t)n.root.y+0x80);
            if(y<0)n.anchor.y=y>>1;
        }
    }
    if(!n.in_air)n.launch_armed=0;
    if(n.turn_frames) {
        n.turn_frames=s16(n.turn_frames-1);n.heading=(uint16_t)(n.heading+n.turn_step);
        if(n.throw_state==1)n.facing=n.heading;
    }
    if(n.push_frames>0){push=n.push_speed;--n.push_frames;}
    if(n.push_table_frames>0) {
        if(!t->push || n.push_cursor+2u>t->push_size)return 0;
        --n.push_table_frames;push+=le16(t->push+n.push_cursor);n.push_cursor+=2u;
        if(n.push_repeat>0)push+=n.push_repeat*40+10;
    }
    if(push) {
        if(!t3_psx_polar(t,push,(uint16_t)n.push_direction,&dx,&dz))return 0;
        n.anchor.x=w32((int64_t)n.anchor.x+dx);n.anchor.z=w32((int64_t)n.anchor.z+dz);
    }
    tracking(&n,opp,vm,t);
    if(n.attack_alert>0)--n.attack_alert;
    if(n.hit_cooldown>0)--n.hit_cooldown;
    n.active_segments=pose<m->active_first || pose>m->active_last?0u:
                      (n.attack_segment_count==2u?2u:1u);
    n.in_air=(uint8_t)(n.juggle_count || (pose>=m->air_start && pose<=m->air_end));
    if(pose==m->air_end && vm->clock.hold_frames==-1 && pose!=1 && n.air_phase!=2u) {
        if(!n.was_hit_this_move || n.air_kind==5u || (n.state_class!=9u && !(n.state&4u)))n.landed_a=1u;
        else n.landed_b=1u;
    }
    if(n.recover_frames) {n.recover_frames=s16(n.recover_frames-1);if(n.recover_frames<0 || n.hit_clean)n.recover_frames=0;}
    if(n.recover_frames<31 && n.recover_mash) {
        int mash=pressed&2u?3:pressed?1:0;
        n.recover_mash=s16(n.recover_mash-1-mash);if(n.recover_mash<0)n.recover_mash=0;
    }
    if(n.damage) {
        int first=m->active_first>=3u?m->active_first-3:1;
        if(pose>=first && pose<=m->active_first)n.about_to_hit=1u;
    }
    if(m->flags&0x400u)n.about_to_hit=1u;
    if(n.character==17u && n.opp_character!=17u)n.about_to_hit=1u;
    if(!(m->flags&0x4000u)) {
        if(n.step_kind) {
            n.state=direction==4u?0x1052u:direction==1u?0x2829u:m->state_word;
            if((n.state&0x100u) && !n.human_guard)n.state&=~0x18u;
        }
    } else if(n.human_guard) {
        if(direction==4u || direction==5u)n.state=0x1052u;
        else if(direction==1u || direction==2u)n.state=0x2829u;
    } else {
        if(direction==4u)n.state=0x1052u;else if(direction==1u)n.state=0x2829u;
    }
    n.guard=!n.in_air && n.relative_angle<0x4000u?(uint16_t)(n.state&0x18u):0u;
    if(n.state_class==9u && m->air_start && pose>=m->air_start)n.state|=4u;
    if(!n.hit_clean && !opp->hit_clean) {
        if(n.power_timer) {n.power_timer=s16(n.power_timer-1);n.guard&=(uint16_t)~0x18u;if(n.power_timer<0)n.power_timer=0;}
    } else n.power_timer=0;
    if(n.in_air)n.push_repeat_timer=0;
    if(n.push_repeat_timer<1)n.push_repeat_timer=0;else --n.push_repeat_timer;
    if(!n.push_repeat_timer)n.push_repeat=0;
    *state=n;return 1;
}
T3_SIM_WRAP(int,t3_psx_ground_step,T3_SIM_GROUND,
    (t3_psx_ground_state_t* state,const t3_psx_ground_state_t* opp,
                       const t3_move_vm_t* vm,const t3_move_vm_t* opponent_vm,
                       const t3_psx_ground_tables_t* t,uint8_t direction,uint8_t pressed),
    (state,opp,vm,opponent_vm,t,direction,pressed))
