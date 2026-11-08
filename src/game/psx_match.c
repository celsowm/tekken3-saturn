/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Original-state integration using the pinned Tekken3Remake frame sequence
 * and RootReanchor. Changes: caller-owned portable state/tables and atomic
 * failure; no prototype physics/health policy is called or copied.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "sim_profile.h"
#include "psx_match.h"
#include <string.h>
#include "sh2_divide.h"
#include "move_index.h"
#include "attack_records.h"
#include "psx_contact.h"
#include "branch_rules.h"
#include "psx_ai_game.h"
#include <saturn/int_math.h>
#if defined(T3_MATCH_FAILURE_TRACE)
volatile uint32_t g_t3_match_failure_phase;
#define T3_MATCH_PHASE(value) (g_t3_match_failure_phase=(value))
#else
#define T3_MATCH_PHASE(value) ((void)0)
#endif
/* The round flow keeps the input mode of the first two records; Tekken Force's third follows the second. */
#define FLOW_INPUT(n,i) ((n)->flow.input[(i)<2u?(i):1u])
static int replay_playing(const t3_psx_match_t* m) {
    return m->config->enable_rounds==2 && m->replay.mode>=1 && m->replay.mode<=4;
}

/* The row a slot names in the merged move index. The game compares row
 * pointers: a row of the linked opponent bank carries 0x20000000. */
static uint32_t blend_row(const t3_divmot_t* bank,unsigned slot) {
    const t3_divmot_t* source;uint32_t id;
    if(!t3_move_index_entry(bank,slot,&source,&id))return T3_DIVMOT_EMPTY_SLOT;
    return id>=0x40000000u || source==bank?id:id|0x20000000u;
}
/* FUN_80038B60: a throw victim takes its partner's scale. */
static unsigned actor_scale(const t3_psx_match_t* match,unsigned actor) {
    const t3_psx_match_config_t* c=match->config;
    if(match->fighter[actor].ground.throw_state<0)actor=T3_PSX_OPP(match,actor);
    return c->actor_scale_percent[actor]?c->actor_scale_percent[actor]:c->scale_percent;
}
typedef struct { const t3_psx_match_t* match;const t3_divmot_t* bank;unsigned scale_percent; } blend_user_t;
static int blend_sample(void* user,uint32_t row,unsigned frame,int16_t root[3],t3_psx_matrix_t out[18]) {
    const blend_user_t* u=user;
    const t3_psx_match_config_t* c=u->match->config;
    const t3_divmot_t* bank=(row&0x40000000u)?c->common:(row&0x20000000u)?u->bank->linked:u->bank;
    t3_move_record_t move;t3_motion_stream_t stream;
    int16_t pose[T3_PSX_POSE_CHANNELS];t3_psx_matrix_t fk[18];uint32_t mask;
    int16_t root_xx;
    if(!bank || !t3_divmot_move(bank,row&((row&0x40000000u)?0x3FFFFFFFu:0x1FFFFFFFu),&move) ||
       t3_motion_open(bank,c->common,move.animation_offset,&stream)!=1)return 0;
    if(c->local_pose_cache) {
        uint32_t fk_mask;
        if(!t3_psx_local_pose(&stream,frame,&c->spline,&c->ground_tables.trig,&c->ik,
               c->local_pose_cache,pose,out,&fk_mask,&mask,&root_xx))return 0;
    } else if(t3_motion_sample_cached(c->sample_cache,&stream,frame,&c->spline,pose)!=1 ||
       !t3_psx_pose_fk(pose,&c->ground_tables.trig,fk,&mask) ||
       t3_psx_pose_ik(pose,&c->ik,fk,mask,out,&mask)!=1)return 0;
    /* fight_sim _pose_slots: slots IK leaves unwritten stay zero. */
    for(unsigned k=0;k<18u;++k)if(!(mask&(1u<<k)))out[k]=(t3_psx_matrix_t){{{0}}};
    for(unsigned i=0;i<3;++i) {
        int32_t value=sat_floor_shift_i32((int32_t)pose[i]*(int32_t)(u->scale_percent*4096u/100u),12);
        uint16_t bits=(uint16_t)value;
        root[i]=(int16_t)(bits<32768u?(int32_t)bits:(int32_t)bits-65536);
    }
    return 1;
}

static uint32_t isqrt(uint32_t n) {
    uint32_t guess=1,rest=n;
    if(!n)return 0;
    while(guess<rest){guess<<=1;rest>>=1;}
    /* The callers' x^2 + z^2 stays below 2^31: there the signed divider
     * gives the same quotient. */
    for(;;){
        const uint32_t before=guess;
        const uint32_t q=n<=(uint32_t)INT32_MAX?(uint32_t)t3_div32((int32_t)n,(int32_t)before):n/before;
        guess=(q+before)>>1;if(guess>=before)return before;
    }
}
/* SituationClassify (0x800313B0): the throw situation code of branch
 * conditions 2..0x17. The tables are indexed by the opponent quadrant
 * (+0x40): q 0..3. BranchFind recomputes it per list from facts that a list
 * walk does not change, so one value per branch evaluation is the same. */
int16_t t3_psx_throw_situation(const t3_psx_ground_state_t* self,int contact,
                               const t3_psx_ground_state_t* opp) {
    static const int8_t codes[4][4]={{2,4,3,5},{6,8,7,9},{10,12,11,13},{14,16,15,17}};
    const uint32_t s=opp->state;
    const unsigned q=self->opponent_quadrant;
    int row=-1;
    if(!contact || opp->in_air || opp->about_to_hit || q>3u)return -1;
    if((int16_t)self->relative_angle<0x2AAA) {
        if(s&0x80u)row=(s&0x200u)?3:2;
        else if(s&0x40u)row=0;
        else if(s&0x20u)row=1;
        return row<0?-1:codes[row][q];
    }
    if((int16_t)self->relative_angle>0x5555 && (s&2u))return q==0?0x12:q==2?0x13:-1;
    return -1;
}
void t3_psx_match_branch_facts(const t3_psx_match_t* n,unsigned i,t3_branch_context_t* facts) {
    const t3_psx_match_config_t* c=n->config;
    const t3_psx_fighter_t *f=&n->fighter[i],*opp=&n->fighter[T3_PSX_OPP(n,i)];
    *facts=(t3_branch_context_t){0};
    facts->known=T3_FACT_IDENTITY|T3_FACT_DISTANCE|T3_FACT_ORIENTATION|T3_FACT_RECOVERY|
                 T3_FACT_COLLISION|T3_FACT_CONTACT|T3_FACT_HEALTH|T3_FACT_POWER|T3_FACT_OPPONENT|
                 T3_FACT_THROW|T3_FACT_BUTTON_TAPS;
    facts->tap_lp=f->taps.lp;facts->tap_rp=f->taps.rp;facts->tap_both=f->taps.both;
    facts->throw_situation=t3_psx_throw_situation(&f->ground,f->contact,&opp->ground);
    if(c->enable_rounds==2)facts->known|=T3_FACT_SIDE;
    facts->side_flag=f->side_flag;
    facts->opponent_bank=t3_psx_match_bank(c,T3_PSX_OPP(n,i))->bank_type;facts->own_character=c->character[i];
    facts->distance=(int32_t)f->ground.distance;
    /* Kuma and True Ogre read as 0x28 closer, each. */
    facts->adjusted_distance=facts->distance-0x28*((c->character[i]==11u||c->character[i]==20u)+
                                                  (c->character[T3_PSX_OPP(n,i)]==11u||c->character[T3_PSX_OPP(n,i)]==20u));
    facts->relative_angle=f->ground.relative_angle;facts->heading_delta=f->ground.heading_delta;
    facts->facing_quadrant=f->ground.facing_quadrant;facts->opponent_quadrant=f->ground.opponent_quadrant;
    facts->recover_frames=f->ground.recover_frames;facts->recover_mash=f->ground.recover_mash;
    facts->body_contact=f->body.contact;facts->contact=f->contact;
    /* FUN_80045260 clears the hit flags every frame: they hold only on the frame of the hit. */
    facts->guarded=opp->got_hit?opp->last_guarded:0;facts->counter_hit=opp->got_hit?opp->last_counter:0;facts->whiffed=f->whiffed;
    facts->opponent_guard=opp->ground.guard;facts->opponent_in_air=opp->ground.in_air;
    facts->own_health=f->health_q16!=0;facts->own_power_timer=f->ground.power_timer;
    facts->own_attack=f->vm.move.attack_word;facts->opponent_attack=opp->vm.move.attack_word;
    facts->opponent_state=opp->ground.state;facts->opponent_damage=(uint16_t)opp->ground.damage;
    facts->opponent_frame=opp->vm.clock.pose_frame;facts->opponent_active_last=opp->vm.move.active_last;
    facts->opponent_air_start=opp->vm.move.air_start;facts->opponent_air_end=opp->vm.move.air_end;
    /* The opponent's pending transition (+0xA2), 0 once started. */
    facts->opponent_transition=opp->vm.pending_valid?opp->vm.queued.transition:0u;
    facts->step_cooldown=f->ground.step_cooldown;
}
/* The first slot of a sparse reaction record (2-byte index, 42 bytes). */
static int reaction_first_slot(const t3_psx_match_config_t* c,unsigned index,unsigned* slot) {
    if(!c->reactions || c->reactions_size%44u)return 0;
    for(size_t at=0;at<c->reactions_size;at+=44u) {
        const uint8_t* p=c->reactions+at;
        if((unsigned)(p[0]|(p[1]<<8))==index){*slot=(unsigned)(p[2]|(p[3]<<8));return 1;}
    }
    return 0;
}
/* The original keeps one branch kind (+0x8B): a queued move's, else the
 * running move's. A forced start keeps it, and the window (+0x6C). */
static unsigned kept_branch_kind(const t3_move_vm_t* v) {
    return v->pending_valid?v->queued.branch_kind:v->current_branch_kind;
}
/* ThrowVictimStart (0x8002D9B0): the victim's next move is the thrower's
 * pending move's +0x32 entry (a reaction record's first slot, or for 0x1nnn
 * g_throwVictimSlots[n]), from frame 1 with transition 0x22; the thrower's
 * half-turn bit moves to the victim. */
static int throw_victim_start(t3_psx_match_t* n,unsigned victim,unsigned thrower) {
    const t3_psx_match_config_t* c=n->config;
    t3_move_vm_t *v=&n->fighter[victim].vm,*t=&n->fighter[thrower].vm;
    const unsigned value=t->pending_move.reaction;
    unsigned slot,kind;
    if(value&0xF000u) {
        const size_t at=2u*(value&0xFFFu);
        if(!c->throw_victim_slots || at+2u>c->throw_victim_slots_size)return 0;
        slot=(unsigned)(c->throw_victim_slots[at]|(c->throw_victim_slots[at+1u]<<8));
    } else if(!reaction_first_slot(c,value,&slot))return 0;
    kind=kept_branch_kind(v);
    v->pending_valid=0;v->clock.pose_frame=1;
    if(!t3_move_vm_queue(v,slot,34u,kind,1,-1))return 0;
    v->pending_flags=(uint8_t)(t->pending_flags&0x80u);
    t->pending_flags&=(uint8_t)~0x80u;
    return 1;
}
/* The victim's knock-down timing from the thrower's starting move. */
static void thrown_recovery(t3_psx_ground_state_t* g,const t3_move_record_t* m,const t3_move_record_t* other) {
    int32_t frames,mash;
    if(!other->hold_frame) {
        const int32_t diff=(int32_t)other->length-m->length;
        frames=diff<1?other->length:m->length+diff;
        if(frames>300)frames=300;
    } else frames=other->hold_frame+m->length;
    mash=(int16_t)((int16_t)other->damage_class*0x20/0x2D+0x22);
    g->recover_frames=(int16_t)frames;
    g->recover_mash=(int16_t)(mash<34?34:mash>66?66:mash);
}
/* The fighters' screen x (+0x1278) as the last camera frame left it. */
static const int16_t* screen_of(const t3_psx_match_t* match) {
    if(match->config->camera)return match->camera.screen_x;
    return match->screen_known?match->screen_x:NULL;
}
/* PairwiseDistances (0x8004391C), two fighters: the pair's distance and
 * direction, then per fighter its distance and target direction, or a
 * screen-based stand-in with a fixed facing or an opponent off screen. */
#if T3_PSX_FIGHTERS>2
/* The same for any number of fighters, as the original's loop over every pair and then over every fighter
 * against its own opponent (0x8004391C with its count argument). A fighter whose opponent is off screen
 * (or that has a fixed facing) gets the screen-based stand-in. */
static int pair_distance(t3_psx_match_t* match) {
    const int16_t* screen=screen_of(match);
    uint8_t off[T3_PSX_FIGHTERS];
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)off[i]=(uint8_t)((screen && (uint16_t)screen[i]>=0x191u) || match->off_screen_extra[i]);
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        const unsigned o=T3_PSX_OPP(match,i),big=i>o?i:o,small=i>o?o:i;
        t3_psx_ground_state_t* g=&match->fighter[i].ground;
        const int64_t x=(int64_t)match->fighter[big].ground.root.x-match->fighter[small].ground.root.x;
        const int64_t z=(int64_t)match->fighter[big].ground.root.z-match->fighter[small].ground.root.z;
        uint16_t direction;
        if(x< -32767 || x>32767 || z< -32767 || z>32767)return 0;
        const uint32_t distance=isqrt((uint32_t)(x*x+z*z));
        match->pair_dist[i]=distance;
        match->pair_all[small+big-1u]=distance;
        if(!screen){g->distance=distance;continue;}
        if(!t3_psx_atan2(&match->config->ground_tables,(int32_t)z,(int32_t)x,&direction))return 0;
        if(!g->fixed_facing && !off[o]) {
            g->distance=distance;
            g->target_dir=(uint16_t)(o<i?direction:direction-0x8000u);
        } else {
            int32_t s=screen[i];
            s=0xB4-(s<0?-s:s);
            s=0xB4-(s<0?-s:s);
            s=s<0?-s:s;
            g->distance=(uint32_t)(s*10);
            g->target_dir=(uint16_t)(g->fixed_facing_side?0xC000u:0x4000u);
        }
    }
    match->fighter_distance=match->fighter[0].ground.distance;
    return 1;
}
#else
static int pair_distance(t3_psx_match_t* match) {
    const int64_t x=(int64_t)match->fighter[1].ground.root.x-match->fighter[0].ground.root.x;
    const int64_t z=(int64_t)match->fighter[1].ground.root.z-match->fighter[0].ground.root.z;
    const int16_t* screen=screen_of(match);
    uint32_t distance;uint16_t direction;
    if(x< -32767 || x>32767 || z< -32767 || z>32767)return 0;
    distance=match->pair_distance=isqrt((uint32_t)(x*x+z*z));
    if(!screen) {
        match->fighter[0].ground.distance=match->fighter[1].ground.distance=distance;
        match->fighter_distance=distance;
        return 1;
    }
    if(!t3_psx_atan2(&match->config->ground_tables,(int32_t)z,(int32_t)x,&direction))return 0;
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        t3_psx_ground_state_t* g=&match->fighter[i].ground;
        if(!g->fixed_facing && (uint16_t)screen[T3_PSX_OPP(match,i)]<0x191u) {
            g->distance=distance;
            g->target_dir=(uint16_t)(i?direction:direction-0x8000u);
        } else {
            int32_t s=screen[i];
            s=0xB4-(s<0?-s:s);
            s=0xB4-(s<0?-s:s);
            s=s<0?-s:s;
            g->distance=(uint32_t)(s*10);
            g->target_dir=(uint16_t)(g->fixed_facing_side?0xC000u:0x4000u);
        }
    }
    /* g_fighterDistance (0x800AFF04): fighter 0's, stand-in included. */
    match->fighter_distance=match->fighter[0].ground.distance;
    return 1;
}
#endif
static int root_pose(const t3_psx_match_t* match,t3_psx_fighter_t* fighter,unsigned frame,int16_t root[3]) {
    const t3_motion_stream_t* stream;
    const t3_move_vm_t* vm=&fighter->vm;
    return t3_motion_open_cached(&fighter->root_cache,vm->root_bank,vm->common,
                                 vm->root_move.animation_offset,&stream)==1 &&
           t3_motion_sample_root(stream,frame,&match->config->spline,root)==1;
}
static int T3_SIM_IMPL(place)(t3_psx_match_t* match,t3_psx_fighter_t* f) {
    int16_t xyz[3],pose[T3_PSX_POSE_CHANNELS]={0};
    t3_psx_joint_t result;
    const t3_psx_match_config_t* c=match->config;
    const unsigned scale_percent=actor_scale(match,(unsigned)(f-match->fighter));
    const blend_user_t user={match,t3_psx_match_bank(c,(unsigned)(f-match->fighter)),scale_percent};
    int32_t scale=(int32_t)(scale_percent*4096u/100u);
    if(f->vm.clock.root_frame<1 || f->vm.clock.pose_frame<1)return 0;
    if(f->ground.anchor_dirty) {
        int frame=f->vm.clock.root_frame-f->vm.clock.frame_step-1;
        const t3_vec3i_t zero={0,0,0};
        if(!root_pose(match,f,(unsigned)(frame<0?0:frame),xyz))return 0;
        pose[0]=xyz[0];pose[1]=xyz[1];pose[2]=xyz[2];
        if(!t3_psx_pose_world_root(pose,scale_percent,zero,f->ground.facing,f->ground.heading,
                                  &c->ground_tables.trig,&result))return 0;
        f->ground.anchor.x=f->ground.root.x-result.origin.x;
        f->ground.anchor.z=f->ground.root.z-result.origin.z;
        f->ground.anchor_dirty=0;
    }
    if(!root_pose(match,f,(unsigned)f->vm.clock.root_frame-1u,xyz))return 0;
    pose[0]=xyz[0];pose[1]=xyz[1];pose[2]=xyz[2];
    /* Keep the displayed root displacement explicit for tracking modes that
     * read its sign. */
    {
        int64_t x=(int64_t)xyz[0]*scale;
        int32_t scaled=(int32_t)(x>=0?x/4096:-((-x+4095)/4096));
        uint16_t bits=(uint16_t)scaled;
        f->ground.root_dx=(int16_t)(bits<32768u?(int32_t)bits:(int32_t)bits-65536);
    }
    if(!t3_psx_pose_world_root(pose,scale_percent,f->ground.anchor,f->ground.facing,
                              f->ground.heading,&c->ground_tables.trig,&result))return 0;
    if(c->rsqrt.data) {
        t3_psx_blend_facts_t facts={0};
        const t3_move_vm_t* v=&f->vm;
        facts.row=blend_row(user.bank,replay_playing(match)?v->root_slot:v->slot);
        facts.pending_row=v->pending_valid?blend_row(user.bank,v->pending_slot):0;
        if(v->move.stance_slot>=T3_DIVMOT_MOVE_SLOTS)return 0;
        facts.stance_row=blend_row(user.bank,v->move.stance_slot);
        facts.pose_frame=v->clock.pose_frame;facts.frame_step=v->clock.frame_step;
        facts.entry_frame=v->queued.entry_frame;facts.previous_event_frame=f->previous_pose_frame;
        facts.decoded_root_dy=xyz[1];facts.throw_state=f->ground.throw_state;
        facts.length=v->move.length;facts.active_first=v->move.active_first;facts.changed=v->clock.move_changed;
        facts.branch_kind=v->pending_valid?v->queued.branch_kind:v->current_branch_kind;
        facts.transition=v->queued.transition;facts.pending=v->pending_valid;
        facts.suppressed=(v->move.flags&0x100u)!=0 || f->blend_guarded_previous || f->blend_in_reaction;
        facts.no_forward=(v->move.flags&0x200u)!=0 || (v->pending_flags&64u)!=0 ||
            f->ground.juggle_count || (f->ground.in_air && f->ground.was_hit_this_move);
        facts.playback=(uint8_t)replay_playing(match);
        if(c->enable_rounds==2 && match->flow.blend_hold>0 &&
           (!facts.playback || !(match->replay.play&1))) {
            facts.global_hold=1;--match->flow.blend_hold;
        }
        if((!facts.playback || !(match->replay.play&1)) &&
           !t3_psx_blend_prepare(&f->blend,&facts,blend_sample,(void*)&user))return 0;
        result.origin.y=f->ground.anchor.y+t3_psx_blend_root_y(&f->blend,
            (int16_t)(result.origin.y-f->ground.anchor.y));
    }
    if(f->ground.air_phase==2u){result.origin.x=f->ground.anchor.x;result.origin.z=f->ground.anchor.z;}
    else if(f->ground.air_phase==1u)result.origin=f->ground.anchor;
    f->ground.root=result.origin;
    return 1;
}
T3_SIM_WRAP_ACTOR(static int,place,T3_SIM_ROOT,
    (t3_psx_match_t* match,t3_psx_fighter_t* f),
    (match,f),(unsigned)(f-match->fighter))

/* FighterMovePhysics' hand shape (moves.md, physics step 9): the costume's hit
 * shape on a clean hit; otherwise, unless a hand event ran in this move, its
 * attack shape at the start of a non-damage move during the fight and within
 * 8 frames before a damage move's active window. */
#if defined(T3_PSX_HAND_CHANNELS)
static void face_shape(const t3_psx_match_t* n,t3_psx_fighter_t* f,unsigned i) {
    const t3_psx_match_config_t* c=n->config;
    if(f->vm.clock.move_changed)f->hand_event_move=0; /* cleared as the move starts */
    if(f->ground.hit_clean){t3_psx_hands_command(&f->hands,3u,c->face_hit[i],10);return;}
    if(f->hand_event_move)return;
    const int fight=c->enable_rounds!=2 || n->flow.state==T3_ROUND_FIGHT || n->flow.state==T3_ROUND_RESULT;
    if(f->vm.clock.move_changed && !f->ground.damage && !f->ground.in_reaction && fight)
        t3_psx_hands_command(&f->hands,3u,c->face_attack[i],10);
    if(f->ground.damage && (int)f->vm.move.active_first-(int)f->vm.clock.pose_frame<8)
        t3_psx_hands_command(&f->hands,3u,c->face_attack[i],10);
}
#endif

/* HandFaceCommand's level channel for True Ogre: shapes 2 and 3 raise it, the others lower it. */
static void ogre_command(t3_psx_fighter_t* f,unsigned shape,int speed) {
    if(!speed)speed=1;
    const int rate=speed<0?256/(1-speed):256/(speed+1);
    const int up=shape==2u || shape==3u;
    f->ogre_target=up?256:0;
    f->ogre_rate=(up?f->ogre_level<256:f->ogre_level>0)?(int16_t)rate:0;
}
static void ogre_events(t3_psx_fighter_t* f) {
    const unsigned hi=(unsigned)f->events.hand>>8u,lo=f->events.hand&0xFFu;
    if(!hi)return;
    /* 20 opens both hands; 21 takes the costume's shape, which is 0 here. */
    if(hi==20u || hi==21u)ogre_command(f,0,(int)lo);
    else ogre_command(f,(hi-14u)&7u,(int8_t)lo);
}
static void ogre_level_step(t3_psx_fighter_t* f) {
    if(f->ogre_level<f->ogre_target) {
        f->ogre_level=(int16_t)(f->ogre_level+f->ogre_rate);
        if(f->ogre_level>=f->ogre_target){f->ogre_rate=0;f->ogre_level=f->ogre_target;}
    } else if(f->ogre_target<f->ogre_level) {
        f->ogre_level=(int16_t)(f->ogre_level-f->ogre_rate);
        if(f->ogre_target>=f->ogre_level){f->ogre_rate=0;f->ogre_level=f->ogre_target;}
    }
}
static int32_t ogre_scale(int32_t v,int32_t scale) {
    const int32_t a=v*scale;
    return (int16_t)(uint16_t)((uint32_t)(a<0?a+0xFF:a)>>8);
}
/* FUN_80033C54: True Ogre's joints 5 and 6 sway with a counter and his level
 * channel; each is placed with the offset of the slot it borrows. */
static int ogre_sway(const t3_psx_match_t* match,t3_psx_fighter_t* f,const t3_psx_part_t* parts,
                     t3_psx_matrix_t complete[T3_PSX_ROTATION_SLOTS],t3_psx_part_t out[T3_PSX_DRAW_PARTS]) {
    const t3_psx_match_config_t* c=match->config;
    const t3_psx_trig_t* trig=&c->ground_tables.trig;
    __builtin_memcpy(out,parts,sizeof(t3_psx_part_t)*T3_PSX_DRAW_PARTS);
    t3_vec3i_t borrowed[2]={{0,0,0},{0,0,0}};
    for(unsigned i=0;i<T3_PSX_DRAW_PARTS;++i) {
        if(parts[i].slot==18u)borrowed[0]=parts[i].offset;
        if(parts[i].slot==5u)borrowed[1]=parts[i].offset;
    }
    if(!match->flow.freeze) {
        f->ogre_sway=(int16_t)(f->ogre_sway+1);
        ogre_level_step(f);
    }
    int32_t height=-f->world[6].origin.y/2;
    height=height<0?0:height>256?256:height;
    const int32_t tilt=c->camera?t3_psx_camera_atan4096(c->camera->tables,complete[5].m[0][0],complete[5].m[0][1])*-4:0;
    const int32_t level=f->ogre_level;
    int32_t amount=height*(256-level);
    amount=(amount<0?amount+0xFF:amount)>>8;
    const unsigned px=((unsigned)f->ogre_sway&0x7Fu)<<5,py=((unsigned)f->ogre_sway&0x3Fu)<<6;
    const int32_t first=((level<0?level+3:level)>>2)+256,other=((level<0?level+7:level)>>3)+256;
    for(unsigned k=0;k<2u;++k) {
        const uint8_t* sx=trig->data+2u*((px+k*0x400u)&0xFFFu);
        const uint8_t* sy=trig->data+2u*((py+k*0x400u)&0xFFFu);
        const int32_t x=(int16_t)(uint16_t)(sx[0]|(sx[1]<<8))*amount;
        const int32_t y=(int16_t)(uint16_t)(sy[0]|(sy[1]<<8))*amount;
        const int16_t angle_x=(int16_t)((x<0?x+0x1FF:x)>>9);
        const int16_t angle_y=(int16_t)(uint16_t)(((uint32_t)(y<0?y+0xFF:y)>>8)+(uint32_t)tilt);
        t3_psx_matrix_t rot;
        if(!t3_psx_euler_matrix(0,angle_x,angle_y,trig,&rot))return 0;
        for(unsigned r=0;r<3u;++r) {
            complete[5u+k].m[r][0]=(int16_t)ogre_scale(rot.m[r][0],first);
            complete[5u+k].m[r][1]=(int16_t)ogre_scale(rot.m[r][1],other);
            complete[5u+k].m[r][2]=(int16_t)ogre_scale(rot.m[r][2],other);
        }
    }
    for(unsigned i=0;i<T3_PSX_DRAW_PARTS;++i) {
        if(parts[i].slot==5u)out[i].offset=borrowed[0];
        if(parts[i].slot==6u)out[i].offset=borrowed[1];
    }
    return 1;
}
static int T3_SIM_IMPL(compose)(t3_psx_match_t* match,unsigned index) {
    t3_psx_fighter_t* f=&match->fighter[index];
    const t3_psx_match_config_t* c=match->config;
    const t3_psx_part_t* parts=c->parts[index];
    const t3_motion_stream_t* stream;
    t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],complete[T3_PSX_ROTATION_SLOTS];
    int16_t root_xx;
    t3_psx_joint_t root;
    uint32_t fk=0,mask=0;
    const unsigned frame=(unsigned)f->vm.clock.pose_frame-1u;
    if(t3_motion_open_cached(&f->pose_cache,f->vm.active_bank,f->vm.common,
                             f->vm.move.animation_offset,&stream)!=1)return 0;
    /* FK/IK consumes the pose stream's own root channels. World placement
     * separately uses root_move, which can differ during transition4/5. */
    f->pose_frames=stream->frames;
    if(c->local_pose_cache) {
        T3_SIM_START(local,T3_SIM_LOCAL_POSE);
        const int posed=t3_psx_local_pose(stream,frame,&c->spline,&c->ground_tables.trig,&c->ik,
               c->local_pose_cache,f->pose,complete,&fk,&mask,&root_xx);
        T3_SIM_END(local);
        if(!posed)return 0;
    } else {
        if(t3_motion_sample_cached(c->sample_cache,stream,frame,&c->spline,f->pose)!=1 ||
           !t3_psx_pose_fk(f->pose,&c->ground_tables.trig,slots,&fk) ||
           t3_psx_pose_ik(f->pose,&c->ik,slots,fk,complete,&mask)!=1)return 0;
        root_xx=slots[0].m[0][0];
    }
    T3_SIM_START(world_root,T3_SIM_WORLD_ROOT);
    const int rooted=t3_psx_pose_world_root(f->pose,actor_scale(match,index),f->ground.anchor,f->ground.facing,
                               f->ground.heading,&c->ground_tables.trig,&root);
    T3_SIM_END(world_root);
    if(!rooted)return 0;
    root.origin=f->ground.root;
    {
        /* A short or singular limb keeps the previous frame's slot. */
        static const uint8_t limb[8]={4,5,8,9,12,13,15,16};
        for(unsigned k=0;k<8u;++k)if(!(mask&(1u<<limb[k]))) {
            if(!f->limbs_valid)return 0;
            complete[limb[k]]=f->limbs[k];mask|=1u<<limb[k];
        }
        if(c->rsqrt.data && !t3_psx_blend_locals(&f->blend,&c->rsqrt,complete))return 0;
        for(unsigned k=0;k<8u;++k)f->limbs[k]=complete[limb[k]];
        f->limbs_valid=1;
    }
    if(c->enable_rounds==2 && match->flow.camera!=2)f->head.active=0;
    else if(f->parts_mask) {
        t3_psx_body_tables_t tables={c->ground_tables,c->body_tables,384u};
        f->head.no_yaw=c->character[index]==17u;
        if(!t3_psx_head_look(&f->head,&tables,f->world,match->fighter[T3_PSX_OPP(match,index)].world,
                            &f->vm.move,f->vm.clock.pose_frame,f->no_look_at,&complete[2]))return 0;
    }
#if defined(T3_PSX_HAND_CHANNELS)
    if(c->character[index]!=20u && !match->flow.freeze)t3_psx_hands_step(&f->hands);
#endif
    t3_psx_part_t ogre_parts[T3_PSX_DRAW_PARTS];
    if(c->character[index]==20u) {
        if(!ogre_sway(match,f,parts,complete,ogre_parts))return 0;
        parts=ogre_parts;
    }
    const int composed=c->world_pose_cache?
        t3_psx_compose_cached(parts,complete,mask,&root,c->world_pose_cache,f->world,&f->parts_mask):
        t3_psx_compose_parts(parts,complete,mask,&root,f->world,&f->parts_mask);
    if(!composed || (f->parts_mask&0x3FFFFu)!=0x3FFFFu)return 0;
    T3_SIM_START(slots_started,T3_SIM_SLOTS);
    const int slotted=t3_psx_parts_to_slots(parts,f->world,f->parts_mask,f->world,&f->parts_mask);
    T3_SIM_END(slots_started);
    if(!slotted || (f->parts_mask&0x3FFFFu)!=0x3FFFFu)return 0;
    f->fk_mask=fk;f->ik_mask=mask&~fk;f->fk_root_xx=root_xx;
    t3_psx_blink_step(&f->blink,f->vm.move.flags,f->vm.move.state_word,&match->camera.rand);
    if(c->camera) {
        t3_psx_camera_matrix_t view;
        t3_psx_match_fighter_view(&match->camera,&view);
        t3_psx_match_draw_screen(match,index,&view,&match->camera.screen_x[index]);
    }
    if(replay_playing(match))return 1;
    T3_SIM_START(shapes,T3_SIM_SHAPES);
    const int shaped=t3_psx_body_shapes(&f->body,f->world,f->parts_mask,
                 c->actor_body_shapes[index]?c->actor_body_shapes[index]:c->body_tables+384,
                 &f->vm.move,f->vm.clock.pose_frame,f->ground.in_air,f->ground.was_hit_this_move);
    T3_SIM_END(shapes);
    return shaped;
}
T3_SIM_WRAP_ACTOR(static int,compose,T3_SIM_COMPOSE,
    (t3_psx_match_t* match,unsigned index),
    (match,index),index)

/* CollisionShapesUpdate's weapon points: spare joints the descriptors of these
 * fighters name outside the active window. Rules from Tekken3Remake
 * docs/research/code/combat.md; PolyForm Noncommercial, NOTICE.md.
 * Required Notice: Copyright (c) 2026 n0madic. */
enum { SPARE_JOINT=18u,SEGMENT_JOINTS=24u,ANY_CHARACTER=0xFFu };
static const struct {uint8_t bank,character,joint,target;int16_t point[3];} g_weapon_points[]={
    {4u,ANY_CHARACTER,10u,18u,{100,-720,0}},{4u,ANY_CHARACTER,10u,19u,{100,-360,0}},
    {14u,20u,11u,22u,{100,-500,0}},{14u,20u,2u,23u,{800,0,600}},
    {19u,ANY_CHARACTER,11u,21u,{200,900,0}},{19u,ANY_CHARACTER,2u,23u,{400,0,100}},
};
static void weapon_points(const t3_psx_fighter_t* f,unsigned bank,unsigned character,
                          t3_vec3i_t spare[SEGMENT_JOINTS-SPARE_JOINT]) {
    __builtin_memset(spare,0,sizeof(t3_vec3i_t)*(SEGMENT_JOINTS-SPARE_JOINT));
    for(unsigned j=SPARE_JOINT;j<T3_PSX_DRAW_PARTS;++j)spare[j-SPARE_JOINT]=f->world[j].origin;
    for(unsigned k=0;k<sizeof g_weapon_points/sizeof g_weapon_points[0];++k) {
        const int32_t point[3]={g_weapon_points[k].point[0],g_weapon_points[k].point[1],g_weapon_points[k].point[2]};
        const t3_psx_joint_t* joint=&f->world[g_weapon_points[k].joint];
        if(g_weapon_points[k].bank!=bank ||
           (g_weapon_points[k].character!=ANY_CHARACTER && g_weapon_points[k].character!=character))continue;
        spare[g_weapon_points[k].target-SPARE_JOINT]=(t3_vec3i_t){
            joint->origin.x+t3_psx_dot12(joint->rotation.m[0],point),
            joint->origin.y+t3_psx_dot12(joint->rotation.m[1],point),
            joint->origin.z+t3_psx_dot12(joint->rotation.m[2],point)};
    }
}
/* A descriptor's joint: Ogre's 0x13 is joint 6; 0x18 and above name no joint. */
static const t3_vec3i_t* segment_joint(const t3_psx_fighter_t* f,const t3_vec3i_t* spare,
                                       unsigned joint,unsigned character) {
    if(joint==0x13u && character==14u)joint=6u;
    if(joint>=SEGMENT_JOINTS)return NULL;
    return joint<SPARE_JOINT?&f->world[joint].origin:&spare[joint-SPARE_JOINT];
}
#if T3_PSX_FIGHTERS>2
/* Tekken Force: an enemy record the level runner has not spawned (no health, invulnerable) strikes and is
 * struck by nothing. */
static int parked(const t3_psx_match_t* match,const t3_psx_fighter_t* f) {
    return match->config->start_force && !f->body.active;
}
#else
#define parked(match,f) 0
#endif
static int attack_shapes(t3_psx_match_t* match,t3_psx_fighter_t* f) {
    t3_attack_sample_t sample;t3_world_attack_t result;
    const t3_move_vm_t* vm=&f->vm;
    const unsigned index=(unsigned)(f-match->fighter);
    const unsigned character=match->config->character[index];
#if T3_PSX_FIGHTERS>2
    /* An enemy record the level runner has not spawned holds no move: it has no attack to place. */
    if(parked(match,f))return 1;
#endif
    int decoded=t3_attack_sample(vm->active_bank,&vm->move,
        vm->move.active_first,0,&sample);
    if(decoded<0)return 0;
    if(!decoded)return f->ground.active_segments==0u;
    f->strike_joint=sample.joint_a[0];
    if(vm->clock.pose_frame>=vm->move.active_first && vm->clock.pose_frame<=vm->move.active_last) {
        /* Projectiles take their segments from the effect buffer, not the record. */
        if(sample.joint_a[0]>=SEGMENT_JOINTS)return 1;
        /* Ogre and Mokujin read the record's alternative point block. */
        decoded=t3_attack_sample(vm->active_bank,&vm->move,vm->clock.pose_frame,
                                 character==14u || character==15u,&sample);
        if(decoded!=1 || !t3_psx_attack_place(&sample,
                (unsigned)(vm->clock.pose_frame-vm->move.active_first),
                f->ground.anchor,f->previous_anchor,f->ground.root,f->ground.heading,f->ground.facing,
                &match->config->ground_tables.trig,&f->strike,&result))return 0;
        /* A layout may bake fewer segments than the descriptor names; the rest
         * keep the points the joints last gave them. */
        for(unsigned k=result.segment_count;k<f->ground.attack_segment_count;++k)
            result.segments[k]=f->strike.segments[k];
        if(result.segment_count<f->ground.attack_segment_count)
            result.segment_count=f->ground.attack_segment_count;
        f->strike=result;
    } else {
        unsigned count=f->ground.attack_segment_count;
        t3_vec3i_t spare[SEGMENT_JOINTS-SPARE_JOINT];
        weapon_points(f,t3_psx_match_bank(match->config,index)->bank_type,character,spare);
        for(unsigned k=0;k<count;++k) {
            const t3_vec3i_t* end=segment_joint(f,spare,sample.joint_a[k],character);
            const t3_vec3i_t* start=sample.joint_b[k]?segment_joint(f,spare,sample.joint_b[k],character):NULL;
            if(end)f->strike.segments[k].end=*end;
            if(start)f->strike.segments[k].start=*start;
        }
        f->strike.segment_count=(uint8_t)count;
    }
    return 1;
}
static int T3_SIM_IMPL(contacts)(t3_psx_match_t* match) {
    t3_hurt_cylinder_t hurt[T3_PSX_FIGHTERS][T3_PSX_HURT_ZONES];
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
#if T3_PSX_FIGHTERS>2
        if(parked(match,&match->fighter[i])){memset(hurt[i],0,sizeof hurt[i]);continue;}
#endif
        if(!t3_psx_hurt_from_parts(match->fighter[i].world,
               match->fighter[i].parts_mask,t3_psx_hurt_profile(match->config->character[i]),hurt[i]))return 0;
    }
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        t3_psx_fighter_t *f=&match->fighter[i],*d=&match->fighter[T3_PSX_OPP(match,i)];
#if T3_PSX_FIGHTERS>2
        if(parked(match,f)) {
            /* Not on the field: nothing strikes and nothing is struck. */
            f->projectile_hit=0;f->geometry=(t3_psx_shadow_result_t){T3_SHADOW_INACTIVE,0,-1,0};
            continue;
        }
#endif
        t3_psx_hit_test_facts_t facts={0};t3_world_attack_t active=f->strike;
        unsigned segment=0;int zone=-1,any=0;
        uint8_t descriptor[4];
        f->projectile_hit=0;
        if(t3_attack_descriptor_joints(f->vm.active_bank,&f->vm.move,descriptor)==1 && descriptor[0]>=SEGMENT_JOINTS) {
            /* HitTestSegments: a projectile tests the segments its owner's objects published. */
            const t3_psx_effect_pool_t* pool=match->config->effects;
            if(pool)for(unsigned k=pool->head;k!=T3_PSX_EFFECT_NONE && zone<0;k=pool->next[k]) {
                const t3_psx_spark_t* o=&pool->object[k];
                if(!(o->flags&2u) || !(o->flags&(i?8u:0x10u)))continue;
                active.segment_count=1;any=1;
                active.segments[0]=(t3_world_segment_t){{o->px,o->py,o->pz},{o->x,o->y,o->z},0};
                zone=t3_psx_attack_contact(&active,hurt[T3_PSX_OPP(match,i)],&segment);
                if(zone< -1)return 0;
            }
            if(zone>=0){f->projectile=active.segments[0];f->projectile_hit=1;}
        } else {
            active.segment_count=f->ground.active_segments;
            if(active.segment_count>f->strike.segment_count)return 0;
            any=active.segment_count!=0;
            if(any)zone=t3_psx_attack_contact(&active,hurt[T3_PSX_OPP(match,i)],&segment);
            if(zone< -1)return 0;
        }
        f->geometry=(t3_psx_shadow_result_t){any?
            (zone>=0?T3_SHADOW_CONTACT:T3_SHADOW_MISS):T3_SHADOW_INACTIVE,0,(int8_t)zone,(int8_t)segment};
        if(zone>=0)++f->contact_frames;
        facts.geometry=f->geometry;facts.attack_word=f->vm.move.attack_word;
        facts.attacker_move_slot=f->vm.slot;facts.attacker_damage=f->ground.damage;
        facts.attacker_freeze=f->vm.move.hit_freeze;facts.attacker_active=f->body.active;
        facts.attacker_invulnerable=f->body.invulnerable!=0u;
        facts.defender_active=d->body.active;facts.defender_invulnerable=d->body.invulnerable!=0u;
        facts.defender_state=(uint16_t)d->ground.state;facts.defender_pose_frame=d->vm.clock.pose_frame;
        facts.defender_air_start=d->vm.move.air_start;facts.defender_air_end=d->vm.move.air_end;
        facts.defender_state_class=d->ground.state_class;facts.defender_step_kind=d->ground.step_kind;
        facts.defender_hit_cooldown=d->ground.hit_cooldown!=0;
        facts.defender_in_air=d->ground.in_air!=0u;facts.defender_in_throw=d->ground.throw_state!=0;
        facts.defender_is_cpu=d->ground.is_cpu;facts.free_slots_mask=3;
#if T3_PSX_FIGHTERS>2
        facts.game_mode=8;      /* Tekken Force: a thrown defender cannot be hit (THROW_LOCK) */
#else
        facts.game_mode=1;
#endif
        facts.hit_done=match->config->enable_combat?f->hit_done:0;
        if(!t3_psx_hit_test_gate(&facts,&f->hit_gate))return 0;
        if(f->hit_gate.status==T3_HIT_GATE_ACCEPT)++f->gate_frames;
    }
    return 1;
}
T3_SIM_WRAP(static int,contacts,T3_SIM_CONTACT,
    (t3_psx_match_t* match),
    (match))

static int T3_SIM_IMPL(bodies)(t3_psx_match_t* match) {
    const t3_psx_match_config_t* c=match->config;
    t3_psx_body_tables_t tables={c->ground_tables,c->body_tables,384u};
    t3_psx_body_actor_t pair[T3_PSX_FIGHTERS];
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        t3_psx_fighter_t* f=&match->fighter[i];
        f->body.anchor=f->ground.anchor;f->body.root=f->ground.root;
        f->body.facing=f->ground.facing;f->body.active_first=f->vm.move.active_first;
        f->body.distance=f->ground.distance;f->body.target_dir=f->ground.target_dir;
        pair[i]=f->body;
    }
    /* World joints/shapes remain those computed before separation, as in source. */
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        const t3_vec3i_t before=pair[i].root;
#if T3_PSX_FIGHTERS>2
        if(match->force_walls_on) {
            /* ArenaBounds in mode 8: the Force walls, then the same ramp clamp. */
            int32_t dx,dz;
            t3_force_walls(&match->force_walls,pair[i].root.x,pair[i].root.z,i!=0u,match->force_view_x,match->force_stage,&dx,&dz);
            const int32_t ramp=pair[i].ramp;
            dx=dx>ramp||dx<-ramp?(dx<1?-ramp:ramp):dx;
            dz=dz>ramp||dz<-ramp?(dz<1?-ramp:ramp):dz;
            pair[i].anchor.x=(int32_t)((uint32_t)pair[i].anchor.x+(uint32_t)dx);pair[i].anchor.z=(int32_t)((uint32_t)pair[i].anchor.z+(uint32_t)dz);
            pair[i].root.x=(int32_t)((uint32_t)pair[i].root.x+(uint32_t)dx);pair[i].root.z=(int32_t)((uint32_t)pair[i].root.z+(uint32_t)dz);
        } else
#endif
        if(!t3_psx_arena_bounds(&pair[i],&pair[T3_PSX_OPP(match,i)],&tables))return 0;
        if(match->throw_link && match->fighter[0].ground.in_throw && match->fighter[1].ground.in_throw) {
            /* ArenaBounds moves a linked throw partner by the same correction. */
            t3_psx_body_actor_t* o=&pair[T3_PSX_OPP(match,i)];
            const int32_t dx=(int32_t)((uint32_t)pair[i].root.x-(uint32_t)before.x);
            const int32_t dz=(int32_t)((uint32_t)pair[i].root.z-(uint32_t)before.z);
            o->anchor.x=(int32_t)((uint32_t)o->anchor.x+(uint32_t)dx);o->anchor.z=(int32_t)((uint32_t)o->anchor.z+(uint32_t)dz);
            o->root.x=(int32_t)((uint32_t)o->root.x+(uint32_t)dx);o->root.z=(int32_t)((uint32_t)o->root.z+(uint32_t)dz);
        }
        if(pair[i].ramp<2000) {
            pair[i].ramp+=match->fighter[i].ramp_step;
            match->fighter[i].ramp_step+=2;
            if(pair[i].ramp>2000){pair[i].ramp=2000;match->fighter[i].ramp_step=2;}
        }
        pair[i].contact=pair[i].contact_with=pair[i].no_push=0;
    }
    /* FUN_80043260: the link follows the number of fighters in a throw. */
    if(match->throw_count<2u)match->throw_link=0;
    else if(match->throw_count_seen!=match->throw_count)match->throw_link=1;
    match->throw_count_seen=match->throw_count;
    /* FUN_80043F40: a fighter in a throw and its partner are not pushed. */
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)if(match->fighter[i].ground.throw_state)pair[0].no_push=pair[1].no_push=1;
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        t3_psx_ground_state_t* g=&match->fighter[i].ground;
        g->anchor=pair[i].anchor;
        g->root=pair[i].root;
        /* FighterVelocity (0x80040CA4): the root's move since last frame. */
        g->velocity.x=(int32_t)((uint32_t)g->root.x-(uint32_t)g->velocity_root.x);
        g->velocity.y=(int32_t)((uint32_t)g->root.y-(uint32_t)g->velocity_root.y);
        g->velocity.z=(int32_t)((uint32_t)g->root.z-(uint32_t)g->velocity_root.z);
        g->velocity_root=g->root;
        if(!attack_shapes(match,&match->fighter[i]))return 0;
    }
#if T3_PSX_FIGHTERS==2
    if((c->enable_rounds!=2 || match->flow.camera<3) &&
       !t3_psx_body_separate(pair,match->fighter[0].ground.distance,&tables))return 0;
#else
    /* The pairs (0,1), (0,2), (1,2), each with the pair table's distance. BodySeparate marks the contact
     * with the other fighter of its pair: remapped here to that fighter's index. */
    if(c->enable_rounds!=2 || match->flow.camera<3) {
        static const uint8_t pairs[3][2]={{0,1},{0,2},{1,2}};
        for(unsigned k=0;k<3u;++k) {
            const unsigned a=pairs[k][0],b=pairs[k][1];
            t3_psx_body_actor_t pr[2]={pair[a],pair[b]};
            const uint8_t with_a=pr[0].contact_with,with_b=pr[1].contact_with;
            pr[0].contact_with=pr[1].contact_with=0;
            if(!t3_psx_body_separate_force(pr,k==0?match->fighter[0].ground.distance:match->pair_all[k],&tables,match->throw_count))return 0;
            const uint8_t new_a=(uint8_t)((pr[0].contact_with&2u)?(1u<<b):0u),new_b=(uint8_t)((pr[1].contact_with&1u)?(1u<<a):0u);
            pair[a]=pr[0];pair[b]=pr[1];
            pair[a].contact_with=(uint8_t)(with_a|new_a);pair[b].contact_with=(uint8_t)(with_b|new_b);
        }
    }
#endif
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        if(pair[i].contact)++match->fighter[i].body_contacts;
        match->fighter[i].body=pair[i];
        match->fighter[i].ground.anchor=pair[i].anchor;
        match->fighter[i].ground.root=pair[i].root;
    }
    return 1;
}
T3_SIM_WRAP(static int,bodies,T3_SIM_BODIES,
    (t3_psx_match_t* match),
    (match))

static int reset_fighters(t3_psx_match_t* n) {
    const t3_psx_match_config_t* config=n->config;
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        n->fighter[i]=(t3_psx_fighter_t){0};
        t3_psx_sound_script_stop(&n->sound[i]);
    }
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        t3_psx_fighter_t* f=&n->fighter[i];
        if(!t3_move_vm_start_phased(&f->vm,t3_psx_match_bank(config,i),config->common,3u))return 0;
        /* The first begin phase advances to pose 1. */
        t3_move_clock_reset(&f->vm.clock,0);
        f->ground.anchor=config->spawn[i].anchor;f->ground.root=config->spawn[i].root;
        f->ground.heading=config->spawn[i].heading;f->ground.facing=config->spawn[i].facing;
        f->ground.state=f->vm.move.state_word;
        f->ground.state_class=(uint8_t)((f->vm.move.state_word>>11)&31u);
        f->ground.human_guard=1;f->ground.guard=(uint16_t)(f->ground.state&0x18u);
        f->body.active=1;f->ramp_step=2;
        f->head.snap=1;
        f->body.root=f->ground.root;f->body.anchor=f->ground.anchor;
        f->body.previous_root=f->ground.root;f->body.placed=f->ground.root;
        f->health_q16=config->max_health_q16;
        t3_history_reset(&f->input);
    }
    return pair_distance(n);
}
static void replay_store(const t3_psx_fighter_t* f,t3_psx_replay_frame_t* r) {
    *r=(t3_psx_replay_frame_t){0};
    r->slot=f->vm.slot;r->src_row=f->blend.src_row;r->dst_row=f->blend.dst_row;
    r->root=f->ground.root;r->anchor=f->ground.anchor;r->heading=f->ground.heading;
    r->src_frame=f->blend.src_frame;r->dst_frame=f->blend.dst_frame;
    r->pose_frame=(uint8_t)f->vm.clock.pose_frame;r->blend_frames=(uint8_t)f->blend.frames;
    r->blend_bits=(uint8_t)((f->blend.mode<<4)|(f->blend.active&15));
    r->power=(uint8_t)f->ground.power_timer;
}
static int place_original_round(t3_psx_match_t* n,int first);
static int actor_scales_valid(const t3_psx_match_config_t* c) {
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)if(c->actor_scale_percent[i]>200u)return 0;
    return 1;
}
#if defined(__sh__)
/* Startup only: keep the diagnostic build's full Master stack reserve.
 * Per-tick combat and pose code retain their measured optimization level. */
__attribute__((optimize("Os")))
#endif
int t3_psx_match_init(t3_psx_match_t* out,const t3_psx_match_config_t* config) {
    t3_psx_match_t n={0};
    if(!out || !config || !config->bank || !config->ground_tables.atan ||
       config->ground_tables.atan_size!=1025u || !config->ground_tables.trig.data ||
       config->ground_tables.trig.size!=T3_PSX_TRIG_BYTES ||
       config->max_health_q16<=0 || config->scale_percent<1u || config->scale_percent>200u ||
       !actor_scales_valid(config) ||
       config->enable_rounds>2 || config->start_intro>1 ||
       (config->start_intro && config->enable_rounds!=2))return 0;
    if(config->effects && (config->enable_rounds!=2 || !config->camera))return 0;
    if(config->enable_body && (!config->parts[0] || !config->parts[1] || !config->body_tables ||
       config->body_tables_size!=432u || !config->ik.ik_blob ||
       config->ik.ik_size!=T3_PSX_IK_BYTES))return 0;
    if(config->enable_combat && (!config->enable_body || !config->reactions ||
       !config->reactions_size || config->reactions_size%44u || config->chip_enabled>1u))return 0;
    if(config->rsqrt.data && (!config->enable_body || config->rsqrt.size!=T3_PSX_RSQRT_BYTES))return 0;
    /* Derived immutable samples may populate even if a later tick fails.
     * Match/fighter/replay-history state remains transactional. */
    t3_motion_sample_cache_reset(config->sample_cache);
    n.config=config;
    t3_psx_round_init(&n.round);
    n.flow=(t3_psx_round_flow_t){.state=T3_ROUND_FIGHT,.played=1,.timer=2400,
        .input={1,1},.camera=2,.to_win=2,.max_rounds=3,.human_mask=3,.draw_wins=1};
    if(config->enable_rounds==2 && config->replay_storage && config->replay_storage->epoch==UINT32_MAX)return 0;
    t3_psx_replay_reset(&n.replay,config->enable_rounds==2 && config->replay_storage?
        config->replay_storage->epoch+1:1);
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)n.round_actor[i].active=1;
#if T3_PSX_FIGHTERS>2
    /* The records' targets before the first roles pass: the player faces fighter 1, the others fighter 0
     * (+0x20 in every recorded Force frame up to the first spawn). */
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)n.opponent[i]=(uint8_t)(i==0);
#endif
    if(!reset_fighters(&n))return 0;
    if(config->camera && (config->enable_rounds!=2 || !config->enable_body ||
       !config->camera->tables || !t3_psx_camera_valid(config->camera->tables)))return 0;
    if(config->start_intro) {
        if(config->start_played>8u || config->start_wins[0]>=n.flow.to_win || config->start_wins[1]>=n.flow.to_win)return 0;
        n.flow.played=config->start_played;
#if T3_PSX_FIGHTERS>2
        /* Tekken Force's single round: one win ends it, one human, the level's own clock. */
        if(config->start_force) {
            n.flow.to_win=n.flow.max_rounds=1;n.flow.human_mask=1;n.flow.draw_wins=0;n.flow.timer=3600;
        }
#endif
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)n.round_actor[i].wins=config->start_wins[i];
        n.camera.rand=config->start_seed;
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
            t3_psx_fighter_t* f=&n.fighter[i];
            /* Newly loaded fighter records are inactive until placement.
             * FighterStartStance binds the row but leaves the pending start
             * for the first MoveStartAll, with a still-zero frame step. */
            f->body.active=0;f->vm.slot=0;f->vm.branch_window=0;
            f->vm.clock=(t3_move_clock_t){0};
            f->ground.anchor=f->ground.root=(t3_vec3i_t){config->start_force?0:i?1000:-1000,0,0};
        }
#if T3_PSX_FIGHTERS>2
        if(config->start_force) {
            /* FUN_800B15EC and the camera's setup (FUN_800B545C) at the level's start. */
            t3_force_level_setup(&n.force,config->force_level);
            n.force_roll=t3_force_camera_setup(&n.force_cam,0,config->force_level==3,&n.force.origin);
            n.force_walls=t3_force_walls_for_level(config->force_level);n.force_walls_on=1;
            n.force_stage=config->force_level==3?0x12u:0u; /* the staircase level's stage */
            if(config->force_level>3)n.flow.timer=0xE10;
        }
#endif
        if(!place_original_round(&n,1))return 0;
    } else if(config->camera && !t3_psx_match_camera_round(&n,1))return 0;
    if(config->ai) {
        if(config->enable_rounds!=2 || !config->enable_combat)return 0;
        n.flow.human_mask=3u&~config->ai->cpu_mask;
        if(!config->start_intro && !t3_psx_ai_game_init_round(config->ai,&n))return 0;
    }
    if(config->enable_rounds==2 && config->replay_storage)config->replay_storage->epoch=n.replay.epoch;
    if(config->effects)t3_psx_effect_pool_round(config->effects);
    *out=n;return 1;
}
static t3_input_t physical_pad(unsigned side,uint16_t held,uint16_t pressed,t3_input_t aliases) {
    t3_input_t pad={(uint16_t)(aliases.held&(T3_IN_GUARD|T3_IN_CROUCH|T3_IN_JUMP)),0};
    const unsigned right=!!(held&0x2000u),left=!!(held&0x8000u);
    if(right!=left)pad.held|=(uint16_t)(right!=(side==1)?T3_IN_FORWARD:T3_IN_BACK);
    const unsigned up=!!(held&0x1000u),down=!!(held&0x4000u);
    if(up!=down)pad.held|=(uint16_t)(up?T3_IN_LEFT:T3_IN_RIGHT);
    static const uint16_t raw[4]={0x80,0x10,0x40,0x20};
    for(unsigned k=0;k<4;++k) {
        if(held&raw[k])pad.held|=(uint16_t)(T3_IN_LP<<k);
        if(pressed&raw[k])pad.pressed|=(uint16_t)(T3_IN_LP<<k);
    }
    return pad;
}
static int advance_core(t3_psx_match_t* n,t3_input_t p0,t3_input_t p1,t3_psx_replay_frame_t record[T3_PSX_FIGHTERS],
                        const t3_psx_match_frame_t* physical) {
    T3_MATCH_PHASE(100);
    const t3_psx_match_config_t* c;
    t3_input_t pads[T3_PSX_FIGHTERS]={p0,p1};
#if T3_PSX_FIGHTERS>2
    if(n)pads[2]=n->third_pad;
#endif
    if(!n || !n->config || n->ticks==UINT32_MAX)return 0;
    c=n->config;
    if(c->enable_rounds==1 && n->round.done) {
        const int32_t hp[2]={n->fighter[0].health_q16,n->fighter[1].health_q16};
        if(t3_psx_round_step(&n->round,hp,c->max_health_q16)) {
            if(!reset_fighters(n))return 0;
        } else {
            /* Result keeps authored recovery/collapse running with no damage
             * and no new player commands. Match end remains latched for Start. */
            pads[0]=pads[1]=(t3_input_t){T3_PSX_INPUT_CLEAR,0};
        }
    }
    /* MoveStartAll (0x80045834): a pending throw (0x21) at its entry frame
     * goes first; its victim starts through ThrowVictimStart, then the
     * thrower. With two, the frame counter picks one. */
#if T3_PSX_FIGHTERS==2
    unsigned order[2]={0u,1u};
#else
    unsigned order[T3_PSX_FIGHTERS];
    for(unsigned k=0;k<T3_PSX_FIGHTERS;++k)order[k]=k;
#endif
    /* The blend's previous event frame is the pose before ThrowVictimStart
     * restarts a victim at pose 1. */
#if T3_PSX_FIGHTERS==2
    const uint16_t previous_pose[2]={(uint16_t)n->fighter[0].vm.clock.pose_frame,
                                     (uint16_t)n->fighter[1].vm.clock.pose_frame};
#else
    uint16_t previous_pose[T3_PSX_FIGHTERS];
    for(unsigned k=0;k<T3_PSX_FIGHTERS;++k)previous_pose[k]=(uint16_t)n->fighter[k].vm.clock.pose_frame;
#endif
    {
        T3_MATCH_PHASE(110);
#if T3_PSX_FIGHTERS==2
        int ready[2]={0,0},thrower=-1;
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
            const t3_move_vm_t* v=&n->fighter[i].vm;
            ready[i]=(c->enable_rounds!=2 || n->fighter[i].body.active) && v->pending_valid &&
                     v->queued.transition==33u && v->queued.entry_frame<=v->clock.pose_frame;
        }
        if(ready[0] && ready[1])thrower=(int)(n->ticks&1u);
        else if(ready[0] || ready[1])thrower=ready[1];
        if(thrower>=0) {
            order[0]=T3_PSX_OPP(n,(unsigned)thrower);order[1]=(unsigned)thrower;
            if(!throw_victim_start(n,order[0],order[1]))return 0;
        }
#else
        /* The fighters with a throw at its entry frame, in index order; the frame counter picks one. The victim
         * goes first, then the thrower, then the other (3 - victim - thrower). */
        unsigned list[T3_PSX_FIGHTERS],count=0;
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
            const t3_move_vm_t* v=&n->fighter[i].vm;
            if((c->enable_rounds!=2 || n->fighter[i].body.active) && v->pending_valid &&
               v->queued.transition==33u && v->queued.entry_frame<=v->clock.pose_frame)list[count++]=i;
        }
        if(count) {
            const unsigned thrower=list[n->ticks%count];
            order[0]=T3_PSX_OPP(n,thrower);order[1]=thrower;order[2]=3u-order[0]-order[1];
            if(!throw_victim_start(n,order[0],order[1]))return 0;
        }
#endif
    }
    for(unsigned j=0;j<T3_PSX_FIGHTERS;++j) {
        T3_MATCH_PHASE(120+j);
        const unsigned i=order[j];
        t3_psx_fighter_t* f=&n->fighter[i];
        f->previous_anchor=f->ground.anchor;
        if(c->enable_rounds==2 && !f->body.active)continue;
        /* MoveStartOrAdvance counts the side-step cooldown down first. */
        if(f->ground.step_cooldown>0)--f->ground.step_cooldown;
        t3_move_record_t before=f->vm.move;
        f->previous_pose_frame=previous_pose[i];
        t3_move_start_facts_t facts={f->got_hit,(uint8_t)(f->health_q16!=0),f->hit_freeze_in,
                                     (int16_t)previous_pose[i]};
        uint8_t flags=f->vm.pending_flags;
        int started=t3_move_vm_begin(&f->vm,facts);
        if(started<0)return 0;
        f->started=(uint8_t)started;
        if(started) {
            if(f->vm.current_transition==34u && f->ground.throw_state>=0)
                t3_psx_sound_script_stop(&n->sound[i]);
            f->taps.condition_used=0;
            f->blend_guarded_previous=f->hit.guarded!=0;
            f->blend_in_reaction=f->hit.hit_clean!=0;
            f->hit_done=0;
            if(!f->contact)f->contact_this_move=0;
            int count=t3_attack_joint_pairs(f->vm.active_bank,&f->vm.move);
            if(count<0 || !t3_psx_ground_start(&f->ground,&n->fighter[T3_PSX_OPP(n,i)].ground,
                    &f->vm,&before,&c->ground_tables,flags,
                    (uint8_t)(f->vm.current_branch_kind==3u || f->vm.current_branch_kind==4u),
                    facts,&f->reaction,(uint8_t)count))return 0;
            if(f->vm.current_transition==34u) {
                /* The thrower has not started yet: its pending move times the fall. */
                const t3_move_vm_t* t=&n->fighter[T3_PSX_OPP(n,i)].vm;
                if(!t->pending_valid)return 0;
                thrown_recovery(&f->ground,&f->vm.move,&t->pending_move);
            }
            if(f->vm.current_transition==30u || f->vm.current_transition==36u) {
                /* The flight starts at the launch entry frame; a launch runs
                 * as air kind 5's move slot (MoveStartOrAdvance's _finish). */
                t3_move_clock_t* k=&f->vm.clock;
                k->pose_frame=k->root_frame=f->vm.last_entry_frame=f->ground.launch_entry;
                k->move_frame=k->move_changed?1:k->pose_frame;k->ballistic=1u;
                if(f->vm.current_transition==30u)
                    f->vm.slot=(uint16_t)(c->ground_tables.air_kinds[5u*4u+2u]|(c->ground_tables.air_kinds[5u*4u+3u]<<8));
            }
        }
    }
    n->throw_count=(uint8_t)(n->fighter[0].ground.in_throw+n->fighter[1].ground.in_throw);
    T3_MATCH_PHASE(130);
    if(!pair_distance(n))return 0;
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        T3_MATCH_PHASE(140+i);
        t3_psx_fighter_t* f=&n->fighter[i];
        if(parked(n,f))continue; /* not on the field: the load's state stands */
        const t3_command_frame_t input=t3_history_at(&f->input,0);
        f->ground.character=c->character[i];f->ground.opp_character=c->character[T3_PSX_OPP(n,i)];
        if(!t3_psx_ground_step(&f->ground,&n->fighter[T3_PSX_OPP(n,i)].ground,&f->vm,
                &n->fighter[T3_PSX_OPP(n,i)].vm,&c->ground_tables,input.direction,input.pressed_buttons))return 0;
        if(f->ground.flight_landed)f->vm.clock.ballistic=0u;
#if defined(T3_PSX_HAND_CHANNELS)
        if(c->character[i]!=20u)face_shape(n,f,i);
#endif
    }
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        T3_MATCH_PHASE(150+i);
#if T3_PSX_FIGHTERS>2
        /* FighterPhysics in mode 8: only a human who faces freely looks at its target. */
        if(c->start_force)n->fighter[i].no_look_at=(uint8_t)(!n->fighter[i].ground.is_cpu && !n->fighter[i].ground.fixed_facing?0:1);
#endif
        n->fighter[i].body.previous_root=n->fighter[i].ground.root;
        n->fighter[i].body.placed=n->fighter[i].ground.root;
        if(c->enable_rounds!=2 || n->fighter[i].body.active) {
            if(!place(n,&n->fighter[i]))return 0;
            if(c->enable_body && !compose(n,i))return 0;
            if(record)replay_store(&n->fighter[i],&record[i]);
        }
    }
    T3_MATCH_PHASE(160);
    if(!t3_psx_match_effects_update(n,0))return 0;
    T3_MATCH_PHASE(161);
    if(c->enable_body && !bodies(n))return 0;
    T3_MATCH_PHASE(162);
    if(c->enable_body && !contacts(n))return 0;
    T3_MATCH_PHASE(163);
    if(c->enable_combat) {
        if(!t3_psx_match_reversals(n))return 0;
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
            T3_MATCH_PHASE(170+i);
            t3_psx_fighter_t* f=&n->fighter[i];
            if(t3_psx_move_events_sample(f->vm.active_bank,&f->vm.move,f->previous_pose_frame,
                                         f->vm.clock.pose_frame,&f->events)!=1)return 0;
            if(c->character[i]==20u)ogre_events(f);
#if defined(T3_PSX_HAND_CHANNELS)
            else if(t3_psx_hands_event(&f->hands,f->events.hand,c->face_attack[i]))f->hand_event_move=1;
#endif
        }
        /* FightFrame runs HitApply every frame, after a KO too; the round's
         * no-damage flag (0x800958A0) keeps health then. */
        if(c->enable_rounds==2 || !c->enable_rounds || !n->round.done) {
            T3_MATCH_PHASE(180);
            int32_t hp[T3_PSX_FIGHTERS];int16_t pose[T3_PSX_FIGHTERS];
            for(unsigned i=0;i<T3_PSX_FIGHTERS;++i){hp[i]=n->fighter[i].health_q16;pose[i]=n->fighter[i].vm.clock.pose_frame;}
            if(!t3_psx_match_apply_hits(n))return 0;
            for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)if(!t3_psx_match_effects_hit(n,i))return 0;
            if(c->enable_rounds==2 && n->replay.mode==7 && n->replay.mark<0 && !c->start_force) {
                for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)if(hp[i]>0 && !n->fighter[i].health_q16) {
                    if(!t3_psx_replay_mark_ko(&n->replay,(unsigned)pose[T3_PSX_OPP(n,i)]))return 0;
                    break;
                }
            }
        } else for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
            n->fighter[i].hit=(t3_psx_hit_apply_result_t){0};
            n->fighter[i].got_hit=0;n->fighter[i].hit_freeze_in=0;
            n->fighter[i].ground.hit_clean=0;n->fighter[i].ground.last_damage=0;
        }
    }
    if(c->effects)t3_psx_flipbook_step(&c->effects->flipbook);
    /* Original sound decisions consume the shared rand before InputSource.
     * The callback owns effect-echo allocation only. Native playback still
     * needs the committed request/replay bridge (combat_events.c remains). */
    if(c->sound_tables.size) {
        t3_psx_sound_facts_t facts[T3_PSX_FIGHTERS]={0};
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
            const t3_psx_fighter_t* f=&n->fighter[i];
            facts[i]=(t3_psx_sound_facts_t){.player=(uint8_t)i,.character=c->character[i],
                .voice_set=c->sound_voice_set[i],.first_joint=f->strike_joint,
                .ko=f->hit.ko_started,.hit_clean=f->ground.hit_clean,.got_hit=f->got_hit,
                .contact=f->contact,.guarded=f->last_guarded,.move_changed=f->vm.clock.move_changed,
                .landed_a=f->ground.landed_a,.landed_b=f->ground.landed_b,
                .health=f->health_q16,.damage=f->ground.damage,.throw_state=f->ground.throw_state,
                .pose_frame=f->vm.clock.pose_frame,.air_first=f->vm.move.air_start,
                .active_first=f->vm.move.active_first};
            if(t3_divmot_sound_list(t3_psx_match_bank(c,i),c->common,f->vm.slot,
                                    &facts[i].commands,&facts[i].command_bytes)!=1)return 0;
        }
        const t3_psx_sound_context_t context={.tables=c->sound_tables,.random=&n->camera.rand,
            .frame_counter=n->frame_counter,.vblank=n->frame_counter,
            .emit=t3_psx_match_effects_sound,.user=n,
            .camera_phase=c->camera?(unsigned)n->camera.phase:(c->enable_rounds==2?(unsigned)n->flow.camera:2u)
#if T3_PSX_FIGHTERS>2
            ,.opponent=n->opponent
#endif
            };
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)if(n->fighter[i].body.active &&
            !t3_psx_fighter_sounds(n->sound,facts,i,&context))return 0;
    }
    /* Source input runs AFTER physics, not before the current ground step.
     * CpuOrPadInput gives a live CPU fighter AiUpdate's pads instead. */
    uint32_t latch_register=0;
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        T3_MATCH_PHASE(190+i);
        const int cpu=c->ai && (c->ai->cpu_mask>>i&1u);
        /* CpuOrPadInput calls FighterUpdateSide again for human input,
         * after FighterAnimate projected this frame's joint 11. The earlier
         * update before CameraFrame is still needed for the camera/CPU. */
        if(c->camera && c->enable_rounds==2 && FLOW_INPUT(n,i)==1 && !cpu) {
            t3_psx_fighter_t* f=&n->fighter[i];
            f->side_flag=f->ground.fixed_facing?f->ground.fixed_facing_side:
                n->camera.screen_x[T3_PSX_OPP(n,i)]<=n->camera.screen_x[i];
            if(physical && physical->physical_inputs)
                pads[i]=physical_pad(f->side_flag,physical->physical[i],physical->pressed[i],pads[i]);
        }
        if(cpu && c->enable_rounds==2 && FLOW_INPUT(n,i)==1) {
            T3_SIM_START(ai,T3_SIM_AI);
            const int fed=t3_psx_ai_game_input(c->ai,n,i,&pads[i]);
            T3_SIM_END(ai);
            if(!fed)return 0;
        }
        if(pads[i].held&T3_PSX_INPUT_CLEAR) {
            t3_history_clear(&n->fighter[i].input);
            n->fighter[i].taps.previous=0;latch_register=5;
            if(c->enable_rounds==2)n->round_actor[i].script_input=0;
        }
        else {
            t3_history_push(&n->fighter[i].input,pads[i]);
            latch_register=(uint32_t)t3_history_at(&n->fighter[i].input,0).pressed_buttons<<4;
        }
    }
    const uint32_t input_frame=c->enable_rounds==2?(uint32_t)n->flow.frame:n->ticks+1u;
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        const t3_command_frame_t input=t3_history_at(&n->fighter[i].input,0);
        t3_psx_tap_latch_step(&n->fighter[i].taps,
            (uint8_t)((input.pressed_buttons<<4)|input.direction),
            input_frame,n->input_frame_seen,&latch_register);
    }
    for(unsigned j=0;j<T3_PSX_FIGHTERS;++j) {
        T3_MATCH_PHASE(200+j);
#if T3_PSX_FIGHTERS==2
        unsigned i=(j+(n->ticks&1u))&1u;
#else
        /* MoveStepAll: fighters 0 and 1 alternate by frame parity, the third goes last and only while the
         * level runs. */
        if(j==2u && n->level_ended)break;
        unsigned i=j<2u?(j+(n->ticks&1u))&1u:j;
#endif
        t3_psx_fighter_t *f=&n->fighter[i],*opp=&n->fighter[T3_PSX_OPP(n,i)];
        t3_branch_context_t facts={0};
        if(!t3_psx_ground_angles(&f->ground,&opp->ground,&c->ground_tables))return 0;
        /* MoveStep (0x800454E0) then takes `dist` from the pair table, without
         * PairwiseDistances' off-screen stand-in. */
        f->ground.distance=T3_PSX_PAIR_DIST(n,i);
        if(c->enable_combat) {
            t3_psx_match_branch_facts(n,i,&facts);
            facts.tap_condition_used=&f->taps.condition_used;
        }
        /* MoveBranchStep returns before BranchFind while the cooldown exceeds 0x19. */
        if(f->ground.step_cooldown<=0x19 &&
           t3_move_vm_queue_input(&f->vm,&f->input,c->enable_combat?&facts:NULL,c->sequences,c->sequence_count)<0)return 0;
        if(c->enable_combat && f->vm.pending_valid && f->vm.queued.branch_kind!=1u) {
            unsigned code=f->vm.queued.transition,slot=f->vm.pending_slot;
            const int unsupported=f->vm.pending_move.active_first && !c->all_attacks && slot!=295u && slot!=355u && slot!=357u;
            if(unsupported) {
                f->vm.pending_valid=0;++f->unsupported_moves;
                f->unsupported_slot=(uint16_t)slot;f->unsupported_transition=(uint16_t)code;
            }
        }
        /* MoveStep pairs a pending thrower with its victim. */
        if(f->vm.pending_valid && f->vm.queued.transition==33u)f->ground.in_throw=opp->ground.in_throw=1u;
    }
    if(c->enable_rounds==1 && !n->round.done) {
        const int32_t hp[2]={n->fighter[0].health_q16,n->fighter[1].health_q16};
        (void)t3_psx_round_step(&n->round,hp,c->max_health_q16);
    }
    n->input_frame_seen=input_frame;
    ++n->ticks;return 1;
}
int t3_psx_match_step(t3_psx_match_t* match,t3_input_t p0,t3_input_t p1) {
    if(!match || !match->config || match->config->enable_rounds==2)return 0;
    t3_psx_match_t n=*match;
    if(!advance_core(&n,p0,p1,0,NULL))return 0;
    T3_SIM_START(copy_out,T3_SIM_COPY);
    *match=n;
    T3_SIM_END(copy_out);return 1;
}
/* Normal VS's later-round FighterPlace. Records persist across rounds; a
 * fighter inactive at placement keeps its pending stance until MoveStartAll. */
#if defined(__sh__)
__attribute__((optimize("Os")))
#endif
static int place_original_round(t3_psx_match_t* n,int first) {
    t3_psx_round_flow_t* r=&n->flow;
    if(r->match_result || r->played>=9)return 0;
    ++r->played;r->frame=r->counter=0;r->timer=2400;r->no_damage=0;
    n->input_frame_seen=0;
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)t3_psx_hud_bar_reset(&n->hud_bars[i]);
    r->camera=r->freeze=r->input[0]=r->input[1]=r->result=0;
    r->state=T3_ROUND_INTRO;r->win_done[0]=r->win_done[1]=0;r->blend_hold=20;
    n->throw_count=0;
    if(n->replay.epoch==UINT32_MAX)return 0;
    t3_psx_replay_reset(&n->replay,n->replay.epoch+1);
    n->replay_skip=0;n->intro_camera_left=first?120:53;
#if T3_PSX_FIGHTERS>2
    if(n->config->start_force) {
        /* Tekken Force has no intro camera (the side-scroller frames the level) and its own clock. */
        n->intro_camera_left=0;r->camera=2;r->timer=3600;
    }
#endif
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        t3_psx_fighter_t* f=&n->fighter[i];t3_psx_ground_state_t* g=&f->ground;
        t3_move_vm_t* v=&f->vm;
        t3_history_clear(&f->input);n->round_actor[i].script_input=0;
        t3_psx_sound_script_stop(&n->sound[i]);
        f->taps.previous=0;f->taps.condition_used=0;
        f->blend.mode=0;
        f->body.placed=g->anchor;
        const unsigned kind=kept_branch_kind(v);
        v->pending_valid=0;
        unsigned slot=1;
#if T3_PSX_FIGHTERS>2
        if(n->config->start_force && i)slot=i==1?2:0; /* the enemy records wait: the first in its stance, the third on its first move; no random draw */
        else
#endif
        if(first) {
            const unsigned held=n->config->start_physical[i];
            slot=held&0x90u?0:2;
            if(!(held&(0x90u|0x44u))) {
                n->camera.rand=n->camera.rand*0x41C64E6Du+0x3039u;
                if((n->camera.rand>>16)&1u)slot=0;
            }
        }
        if(!t3_move_vm_queue(v,slot,0,kind,1,v->branch_window))return 0;
        v->move=v->root_move=v->pending_move;
        v->active_bank=v->root_bank=v->pending_bank;v->root_slot=(uint16_t)slot;
        v->clock.pose_frame=v->clock.root_frame=1;v->last_entry_frame=1;
        if(f->body.active) {
            t3_move_start_facts_t facts={f->got_hit,(uint8_t)(f->health_q16!=0),f->hit_freeze_in,
                                         (int16_t)v->clock.pose_frame};
            if(t3_move_vm_begin(v,facts)!=1 || !t3_psx_ground_start(g,&n->fighter[T3_PSX_OPP(n,i)].ground,v,&v->move,
                &n->config->ground_tables,0,0,facts,&f->reaction,0))return 0;
        }
        v->clock.pose_frame=v->clock.root_frame=(int16_t)(first && v->move.length>150?v->move.length-149:1);
        g->anchor=(t3_vec3i_t){n->config->start_force?0:i?1000:-1000,0,0};
        g->root.x=g->anchor.x;g->root.z=0;
        g->heading=g->facing=g->relative_angle=(uint16_t)(i && !n->config->start_force?0xC000:0x4000);
        f->body.push=(t3_vec3i_t){0,0,0};
        f->world[2].origin=(t3_vec3i_t){g->anchor.x,-0x591,0};
        f->world[1].origin=(t3_vec3i_t){g->anchor.x,-0x41B,0};
        f->health_q16=n->config->max_health_q16;
        v->clock.hold_frames=-1;v->clock.hit_freeze=v->clock.ballistic=0;
        g->slide_state=g->was_hit_this_move=g->air_phase=g->in_air=0;
        g->juggle_count=g->hit_cooldown=g->push_frames=g->push_table_frames=0;
        g->turn_frames=g->turn_step=g->recover_mash=g->recover_frames=0;
        g->push_repeat=g->push_repeat_timer=g->power_timer=g->last_damage=0;
        g->anchor_dirty=g->slide_to_point=g->fixed_facing=0;g->in_throw=0; /* throw_state is kept */
#if T3_PSX_FIGHTERS>2
        /* The roles pass (FUN_800B5EC0) fixes the player's facing while both enemies are invulnerable. */
        if(n->config->start_force && !i)g->fixed_facing=1;
#endif
        f->blend_guarded_previous=f->blend_in_reaction=0;
        f->hit.guarded=f->hit.hit_clean=0;f->ko_collapse=0;
        f->contact=f->whiffed=f->got_hit=0;f->hit_freeze_in=0;
        /* Port diagnostics count successful contacts per round. */
        f->registered_hits=f->registered_blocks=0;
        f->body.active=1;f->body.invulnerable=0;f->no_look_at=0;
#if T3_PSX_FIGHTERS>2
        if(n->config->start_force && i) {
            /* FUN_800B15EC: an enemy record waits inactive with no health, invulnerable, with its stance
             * queued, until the level runner spawns it. */
            f->body.active=0;f->health_q16=0;f->body.invulnerable=1;g->is_cpu=1;
            g->state=0;g->state_class=0;g->human_guard=0;g->guard=0;g->damage=0;
            f->side_flag=1;
            /* The load computed its body spheres from the unposed joints. */
            f->parts_mask=0x3FFFFu;
            if(!t3_psx_body_shapes(&f->body,f->world,f->parts_mask,
                   n->config->actor_body_shapes[i]?n->config->actor_body_shapes[i]:n->config->body_tables+384,
                   &v->move,v->clock.pose_frame,0,0))return 0;
            f->parts_mask=0;
        }
#endif
#if defined(T3_PSX_HAND_CHANNELS)
        t3_psx_hands_command(&f->hands,3u,n->config->face_attack[i],10);
#endif
        f->blend.frames=f->blend.counter=0;f->blend.weight=0;
        for(unsigned k=0;k<3;++k)f->blend.root_delta[k]=0;
        n->round_actor[i].health_left=n->round_actor[i].won=n->round_actor[i].win_pose=0;
        f->head.current[0]=f->head.current[2]=0;f->head.current[1]=0x38E;
        f->head.active=0;f->head.snap=1;
        /* FUN_8002BFCC: air velocity cleared, FUN_80040C80 velocity root
         * from the placed root, screen x on the fighter's own side. */
        g->air_vel_x=g->air_vel_y=g->air_vel_z=0;
        g->velocity_root.x=g->root.x;g->velocity_root.z=g->root.z;
        n->screen_x[i]=(int16_t)(i?100:-100);
        if(n->config->camera)n->camera.screen_x[i]=n->screen_x[i];
    }
    n->screen_known=1;
    if(!pair_distance(n) || !t3_psx_match_camera_round(n,first))return 0;
    if(!first && n->config->effects)t3_psx_effect_pool_round(n->config->effects);
    if(n->config->ai)return t3_psx_ai_game_init_round(n->config->ai,n);
    /* AiInitRound (0x8005993C) resets both records even in human VS.
     * With no CPU bus, retain AiReset's shared-generator effects without
     * allocating its unused candidate/decision records. One rand and one
     * AI LCG advance per record (ai.md / cpu_opponent.gd; NOTICE.md). */
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        n->camera.rand=n->camera.rand*0x41C64E6Du+0x3039u;
        n->camera.ai_rand=n->camera.ai_rand*5u+3u;
    }
    return 1;
}
static int replay_apply_pose(t3_psx_match_t* n,unsigned i,const t3_psx_replay_frame_t* r) {
    t3_psx_fighter_t* f=&n->fighter[i];t3_move_record_t move;const t3_divmot_t* owner;
    if(r->tilt_x || r->tilt_z || !r->pose_frame || r->slot>=T3_DIVMOT_MOVE_SLOTS ||
       !t3_move_vm_resolve(t3_psx_match_bank(n->config,i),n->config->common,r->slot,&move,&owner))return 0;
    f->vm.move=f->vm.root_move=move;f->vm.active_bank=f->vm.root_bank=owner;
    f->vm.root_slot=(uint16_t)r->slot;f->vm.clock.pose_frame=r->pose_frame;
    f->blend.src_row=r->src_row;f->blend.dst_row=r->dst_row;
    f->blend.src_frame=r->src_frame;f->blend.dst_frame=r->dst_frame;
    f->blend.frames=r->blend_frames;f->blend.mode=r->blend_bits>>4;
    f->blend.active=r->blend_bits&15;f->ground.heading=r->heading;
    return 1;
}
static int replay_render(t3_psx_match_t* n) {
    const t3_psx_replay_pair_t *a=0,*b=0;
    if(n->replay.mode!=T3_REPLAY_ENDING) {
        unsigned at=(unsigned)n->replay.play/2;
        a=t3_psx_replay_at(n->config->replay_storage,&n->replay,at);
        b=t3_psx_replay_at(n->config->replay_storage,&n->replay,(at+1)%300);
        if(!a || ((n->replay.play&1) && !b))return 0;
    }
    if(n->replay.mode==T3_REPLAY_FIRST)
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)replay_store(&n->fighter[i],&n->replay.saved[i]);
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        t3_psx_fighter_t* f=&n->fighter[i];
        const t3_psx_replay_frame_t* r=n->replay.mode==T3_REPLAY_ENDING?&n->replay.saved[i]:&a->actor[i];
        if(!replay_apply_pose(n,i,r) || !place(n,f))return 0;
        f->ground.anchor=r->anchor;
        f->ground.root=n->replay.mode==T3_REPLAY_ENDING?r->root:
            t3_psx_replay_root(r->root,b?b->actor[i].root:r->root,(unsigned)n->replay.play&1);
        if(n->config->enable_body && !compose(n,i))return 0;
    }
    return 1;
}
/* One original step on `n` in place. A failure (0) may leave `n` partly
 * advanced; callers needing the old state step a copy. */
#if T3_PSX_FIGHTERS>2
/* Tekken Force: FighterStartMove (the level runner's "start move"): the move at once, as RoundFlow's starts. */
static int force_start_move(t3_psx_match_t* n,unsigned i,unsigned slot) {
    t3_psx_fighter_t* f=&n->fighter[i];
    t3_move_start_facts_t start={f->got_hit,(uint8_t)(f->health_q16!=0),f->hit_freeze_in,(int16_t)f->vm.clock.pose_frame};
    f->blend.mode=0;
    const unsigned kind=kept_branch_kind(&f->vm);
    f->vm.pending_valid=0;f->vm.clock.pose_frame=1;
    /* FACE_OPPONENT (0), started with the fighter as its own opponent: FighterStartMove calls MoveStartOrAdvance(f, f) */
    if(!t3_move_vm_queue(&f->vm,slot,0,kind,1,f->vm.branch_window) || t3_move_vm_begin(&f->vm,start)!=1 ||
       !t3_psx_ground_start(&f->ground,&f->ground,&f->vm,&f->vm.move,
           &n->config->ground_tables,0,0,start,&f->reaction,0))return 0;
    f->hit_done=0;f->taps.condition_used=0;
    return 1;
}
/* The camera runs first in the frame, from the player's root as the last frame left it. */
static void force_camera_frame(t3_psx_match_t* n) {
    if(!n->config->force_trig)return;
    const t3_vec3i_t r=n->fighter[0].ground.root;
    t3_force_camera_frame(&n->force_cam,n->config->force_trig,n->force_roll,r.x,r.z);
}
/* The level runner closes the frame (force.ovl FUN_800B1778): its inputs from the fight and the camera,
 * its writes back to the records; the records of its two slots are fighters 1 and 2. */
/* FUN_800B5EC0 at the start of the frame, from the state the last frame left: who the player faces. */
static int force_roles(t3_psx_match_t* n,const t3_psx_match_frame_t* frame) {
    t3_force_target_inputs_t in={0};
    t3_force_roles_in_t r={{0}};
    in.player_index=0;
    in.pad_pressed=(int16_t)frame->pressed[0];
    in.pair[1]=(int32_t)n->pair_all[0];in.pair[2]=(int32_t)n->pair_all[1];in.pair[3]=(int32_t)n->pair_all[2];
    static const int32_t order[4]={2,0,1,0}; /* force.ovl 0x800B0F04 */
    for(unsigned k=0;k<4;++k)in.quadrant_order[k]=order[k];
    for(unsigned i=0;i<3;++i) {
        r.hidden[i]=n->fighter[i].body.invulnerable!=0;r.screen_x[i]=(uint16_t)n->screen_x[i];r.index[i]=(uint8_t)i;
        if(i) {
            /* FighterRelativeAngles(player, candidate) on a copy: its quadrant and relative angle */
            t3_psx_ground_state_t scratch=n->fighter[0].ground;
            if(!t3_psx_ground_angles(&scratch,&n->fighter[i].ground,&n->config->ground_tables))return 0;
            r.quadrant[i]=(int16_t)scratch.facing_quadrant;r.rel[i]=(int16_t)scratch.relative_angle;
        }
    }
    uint8_t both;
    t3_force_roles(&n->force_target,&in,&r,n->opponent[0],n->opponent,&both);
    n->fighter[0].ground.fixed_facing=both;n->fighter[0].ground.fixed_facing_side=0;
    return 1;
}
static int force_level_frame(t3_psx_match_t* n) {
    const t3_psx_match_config_t* c=n->config;
    t3_force_level_t* s=&n->force;
    if(!c->force_tables || !c->force_script)return 1;
    s->frozen=n->flow.freeze;s->text_off=0;s->round_state=n->flow.state;
    s->camera_x=s->lock_x=n->force_cam.x;
    s->scroll_mode=n->force_cam.mode;
    s->bound[0]=n->force_cam.left;s->bound[1]=n->force_cam.right;s->bound[2]=n->force_cam.min;s->bound[3]=n->force_cam.max;
    s->player_health=n->fighter[0].health_q16;s->dr_b=0;s->player_index=0;
    s->timer=n->flow.timer;
    s->player.x=n->fighter[0].ground.root.x;s->player.z=n->fighter[0].ground.root.z;
    for(unsigned k=0;k<T3_FORCE_SLOTS;++k) {
        const t3_psx_fighter_t* f=&n->fighter[k+1];t3_force_fighter_t* r=&s->fighter[k];
        r->health=f->health_q16;r->fc2=f->body.invulnerable!=0;r->fc3=f->body.active;
        r->anchor_x=f->ground.anchor.x;r->anchor_z=f->ground.anchor.z;
        r->root_x=f->ground.root.x;r->root_z=f->ground.root.z;
        r->f04=f->ground.anchor.y;r->owner=f->last_attacker;
    }
    t3_force_level_step(s,c->force_tables,c->force_script);
    n->flow.timer=s->timer;
    n->force_cam.mode=s->scroll_mode;
    n->force_cam.left=s->bound[0];n->force_cam.right=s->bound[1];n->force_cam.min=s->bound[2];n->force_cam.max=s->bound[3];
    for(unsigned k=0;k<T3_FORCE_SLOTS;++k) {
        t3_psx_fighter_t* f=&n->fighter[k+1];const t3_force_fighter_t* r=&s->fighter[k];
        f->health_q16=r->health;f->body.invulnerable=r->fc2;f->body.active=r->fc3;
        f->ground.anchor.x=r->anchor_x;f->ground.anchor.z=r->anchor_z;f->ground.anchor.y=r->f04;
        f->ground.root.x=r->root_x;f->ground.root.z=r->root_z;
        n->round_actor[k+1].active=r->fc3;
        n->off_screen_extra[k+1]=(uint8_t)(s->slot[k].state==1 && !s->flag_70e8);
    }
    n->level_ended=s->flag_70e8!=0;
    for(unsigned e=0;e<s->event_count;++e) {
        const t3_force_event_t* ev=&s->events[e];
        if(ev->kind==T3_FORCE_EVENT_START_MOVE && ev->a>=0 && ev->a<(int)T3_FORCE_SLOTS &&
           !force_start_move(n,(unsigned)ev->a+1u,(unsigned)ev->b))return 0;
    }
    return 1;
}
#endif
static int step_original(t3_psx_match_t* n,t3_input_t p0,t3_input_t p1,
                         const t3_psx_match_frame_t* frame) {
    T3_MATCH_PHASE(1);
    t3_psx_round_flow_facts_t facts={0};
    t3_psx_round_flow_actions_t actions;
    t3_psx_replay_frame_t record[T3_PSX_FIGHTERS]={{0}};
    n->frame_counter=frame->frame_counter;
    /* Main-loop RNG, before FightMain: it also advances while the director
     * is idle between rounds (Tekken3Remake fight_simulation.begin_frame,
     * original main 0x80028DF4). Camera random draws advance it separately. */
    if(n->config->camera)n->camera.camera_rand=(n->camera.camera_rand+1u)*0x10DCDu;
    n->frame_view_used=0;
#if T3_PSX_FIGHTERS>2
    if(n->config->start_force) {
        force_camera_frame(n);
        if(!force_roles(n,frame))return 0;
    }
#endif
    t3_sim_context((uint32_t)n->flow.state,(uint32_t)n->replay.mode,
        (uint32_t)n->fighter[0].body.active|((uint32_t)n->fighter[1].body.active<<1));
    if(n->config->replay_storage && n->config->replay_storage->epoch!=n->replay.epoch)return 0;
    if(!n->config->camera && frame->screen_known) {
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)n->screen_x[i]=frame->screen_x[i];
        n->screen_known=1;
    }
    if(n->flow.state==T3_ROUND_END && n->flow.match_result) {
        /* Port result screen: preserve the terminal fight while its input
         * event clock continues. Original FightMain has left FightFrame. */
        if(n->ticks==UINT32_MAX)return 0;
        ++n->ticks;
        return 1;
    }
    if(n->flow.state==T3_ROUND_END) {
        T3_MATCH_PHASE(2);
        /* FightMain after FightFrame returns 9: states 5 and 6 run no
         * FightFrame, state 7 only places the next round (FUN_8002AB68),
         * and state 8 resumes FightFrame. The frame counter keeps going. */
        if(n->ticks==UINT32_MAX)return 0;
        if(n->round_gap<2u){++n->round_gap;++n->ticks;return 1;}
        if(!place_original_round(n,0))return 0;
        if(n->config->replay_storage)n->config->replay_storage->epoch=n->replay.epoch;
        n->round_gap=0;++n->ticks;return 1;
    }
    if(n->flow.replay_request) {
        T3_MATCH_PHASE(3);
        if(!t3_psx_replay_start(&n->replay))return 0;
        n->flow.replay_request=0;
        if(n->replay.mode==T3_REPLAY_STARTING)n->flow.blend_hold=20;
    }
    n->hud_replay_entry=(uint8_t)n->replay.mode;
    int ending=n->replay.mode==T3_REPLAY_ENDING;
    T3_MATCH_PHASE(4);
    int playback=t3_psx_replay_update(&n->replay);
    if(playback<0)return 0;
    if(ending) {
        n->flow.blend_hold=20;
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
            n->fighter[i].blend.frames=n->fighter[i].blend.counter=0;n->fighter[i].blend.weight=0;
            for(unsigned k=0;k<3;++k)n->fighter[i].blend.root_delta[k]=0;
        }
    }
    int old_camera=n->flow.camera;uint8_t old_active[2]={n->fighter[0].body.active,n->fighter[1].body.active};
    facts.replay_mode=n->replay.mode;facts.replay_skip=n->replay_skip;facts.frame_counter=frame->frame_counter;
    facts.team_round=1; /* Normal VS setup keeps g_teamRound at 1. */
#if T3_PSX_FIGHTERS>2
    facts.force=n->config->start_force;facts.force_level_ended=n->level_ended;
#endif
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        T3_MATCH_PHASE(10+i);
        t3_psx_fighter_t* f=&n->fighter[i];t3_psx_round_actor_t* a=&n->round_actor[i];
        a->health=f->health_q16;a->maximum=n->config->max_health_q16;
        a->active=f->body.active;a->pose=f->vm.clock.pose_frame;a->length=f->vm.move.length;
        a->bank_type=t3_psx_match_bank(n->config,i)->bank_type;a->is_cpu=f->ground.is_cpu;
        if(f->vm.pending_valid)a->state_word=(int32_t)f->vm.pending_move.state_word;
        else if(n->flow.state==T3_ROUND_RESULT && a->health && a->won>=0 &&
                ((n->flow.result&1) || a->is_cpu) && a->bank_type!=18) {
            t3_move_record_t stance;const t3_divmot_t* owner;
            if(!t3_move_vm_resolve(t3_psx_match_bank(n->config,i),n->config->common,f->vm.move.stance_slot,&stance,&owner))return 0;
            a->state_word=(int32_t)stance.state_word;
        } else a->state_word=0;
        if(i<2u){facts.physical[i]=frame->physical[i];facts.pressed[i]=frame->pressed[i];} /* the pads of the first two records */
    }
    T3_MATCH_PHASE(20);
    if(!t3_psx_round_flow_step(&n->flow,n->round_actor,&facts,&actions))return 0;
    if(actions.stop_sounds)for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)t3_psx_sound_script_stop(&n->sound[i]);
    if(actions.fight_clock_advance)++n->fight_clock;
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        T3_MATCH_PHASE(30+i);
        t3_psx_fighter_t* f=&n->fighter[i];
        f->body.active=(uint8_t)n->round_actor[i].active;
        if(i<2u && actions.start_slot[i]>=0) { /* the round flow knows the first two records */
            t3_move_start_facts_t start={f->got_hit,(uint8_t)(f->health_q16!=0),f->hit_freeze_in,
                                         (int16_t)f->vm.clock.pose_frame};
            f->blend.mode=0;
            if(actions.grounded_mask&(1u<<i))f->ground.anchor.y=0;
            const unsigned kind=kept_branch_kind(&f->vm);
            f->vm.pending_valid=0;f->vm.clock.pose_frame=1;
            if(!t3_move_vm_queue(&f->vm,(unsigned)actions.start_slot[i],6,kind,1,f->vm.branch_window) ||
               t3_move_vm_begin(&f->vm,start)!=1 ||
               /* RoundFlow binds pose_move before MoveStartOrAdvance. Its
                * reanchor/power tail reads the new row, unlike a queued move. */
               !t3_psx_ground_start(&f->ground,&n->fighter[T3_PSX_OPP(n,i)].ground,&f->vm,&f->vm.move,
                   &n->config->ground_tables,0,0,start,&f->reaction,0))return 0;
            f->hit_done=0;
            f->taps.condition_used=0;
        }
    }
    if(actions.animate_before_camera) {
        T3_MATCH_PHASE(40);
        /* RoundFlow animates and draws both fighters with the view the
         * previous frame left, before CameraFrame: the draw moves their
         * screen x, which this frame's PairwiseDistances reads. */
        int camera=n->flow.camera,hold=n->flow.blend_hold;
        t3_psx_camera_matrix_t view;
        const t3_psx_camera_matrix_t* drawn=NULL;
        int16_t* screen=NULL;
        unsigned drew=0;
        if(n->config->camera) {
            t3_psx_match_fighter_view(&n->camera,&view);
            drawn=&view;screen=n->camera.screen_x;
        } else if(frame->fighter_view) {
            drawn=frame->fighter_view;screen=n->screen_x;
        }
        n->flow.camera=old_camera;n->flow.blend_hold=20;
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
            n->fighter[i].body.active=old_active[i];
            if(old_active[i] && (!place(n,&n->fighter[i]) ||
                (n->config->enable_body && !compose(n,i))))return 0;
            if(old_active[i] && drawn) {
                t3_psx_match_draw_screen(n,i,drawn,&screen[i]);++drew;
            }
        }
        /* A supplied view is the only screen source without a camera. */
        if(drawn && drew)n->screen_known=1;
        n->frame_view_used=(uint8_t)(drawn && drew);
        n->flow.camera=camera;n->flow.blend_hold=hold;
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)n->fighter[i].body.active=(uint8_t)n->round_actor[i].active;
    }
    if(actions.clear_blends)for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
            t3_psx_fighter_t* f=&n->fighter[i];
            f->blend.frames=f->blend.counter=0;f->blend.weight=0;
            for(unsigned k=0;k<3;++k)f->blend.root_delta[k]=0;
    }
    /* HudRoundUpdate runs before CameraDirector and before HitApply. Replay
     * playback skips it; its ending branch resumes it on this same tick. */
    if(!playback)for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)
        if(!t3_psx_hud_bar_step(&n->hud_bars[i],n->fighter[i].health_q16,
                               n->config->max_health_q16))return 0;
    /* HudRoundUpdate's ResultWinVoice follow-up (round_hud.gd). It allocates
     * before CameraDirector/EffectsUpdate; the pool's voice wait then runs. */
    if(!playback && n->config->effects && n->flow.counter==1 &&
       (n->flow.state==T3_ROUND_WIN_WAIT || n->flow.state==T3_ROUND_END) &&
       (n->flow.match_result || !n->replay_skip) && n->flow.human_mask==3 &&
       n->round_actor[0].won!=n->round_actor[1].won) {
        unsigned winner=n->round_actor[1].won>n->round_actor[0].won;
        if(n->round_actor[winner].costume!=0x22)t3_psx_effect_alloc(n->config->effects,16);
    }
    if(playback) {
        T3_MATCH_PHASE(50);
        t3_psx_camera_fighter_t before[2];
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)if((n->flow.human_mask&(1u<<i)) && (frame->pressed[i]&0x800u))
            n->replay.skip=n->replay_skip=1;
        t3_psx_match_camera_capture(n,before);
        if(n->ticks==UINT32_MAX || !replay_render(n) ||
           !t3_psx_match_camera_replay(n,before,n->replay.mode==T3_REPLAY_FIRST))return 0;
        if(!t3_psx_match_effects_update(n,1))return 0;
        ++n->ticks;
        return 1;
    }
    /* FighterUpdateSide (0x800464CC) uses the previous frame's screen x. */
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        t3_psx_fighter_t* f=&n->fighter[i];
        if(f->ground.fixed_facing)f->side_flag=f->ground.fixed_facing_side;
        else if(n->config->camera)f->side_flag=n->camera.screen_x[T3_PSX_OPP(n,i)]<=n->camera.screen_x[i];
        else if(frame->side_known)f->side_flag=frame->side[i]!=0;
    }
    T3_MATCH_PHASE(60);
    T3_SIM_START(camera,T3_SIM_CAMERA);
    const int framed=t3_psx_match_camera_frame(n);
    T3_SIM_END(camera);
    if(!framed)return 0;
    /* Original camera setup advances winner phase 3/4 to phase 5 during the
     * following live frame. */
    if(n->flow.camera==3 || n->flow.camera==4)n->flow.camera=5;
    else if(n->flow.camera<2 && n->intro_camera_left>0) {
        n->flow.camera=--n->intro_camera_left?1:2;
    }
    t3_input_t pads[T3_PSX_FIGHTERS]={p0,p1};
#if T3_PSX_FIGHTERS>2
    pads[2]=frame->third;
#endif
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        if(!FLOW_INPUT(n,i))pads[i]=(t3_input_t){T3_PSX_INPUT_CLEAR,0};
        else if(FLOW_INPUT(n,i)==2)pads[i]=(t3_input_t){
            (uint16_t)(n->round_actor[i].script_input&0x1000?T3_IN_LEFT:0),0};
    }
#if T3_PSX_FIGHTERS>2
    n->third_pad=pads[2];
#endif
    T3_MATCH_PHASE(70);
    if(!advance_core(n,pads[0],pads[1],n->replay.mode==7?record:0,frame))return 0;
#if T3_PSX_FIGHTERS>2
    if(n->config->start_force && !force_level_frame(n))return 0;
#endif
    T3_SIM_START(after,T3_SIM_CAMERA);
    const int placed=t3_psx_match_camera_after(n,1);
    T3_SIM_END(after);
    if(!placed)return 0;
    T3_MATCH_PHASE(80);
    if(n->config->replay_storage) {
        t3_psx_replay_storage_t* storage=n->config->replay_storage;
        storage->epoch=n->replay.epoch;
        if(n->replay.mode==7) {
            t3_psx_replay_pair_t* slot=&storage->ring[n->replay.record];
            slot->actor[0]=record[0];slot->actor[1]=record[1];slot->epoch=n->replay.epoch;
        }
    }
    return 1;
}
int T3_SIM_IMPL(t3_psx_match_step_original)(t3_psx_match_t* match,t3_input_t p0,t3_input_t p1,
                               const t3_psx_match_frame_t* frame) {
    t3_psx_match_t n;
    if(!match || !match->config || match->config->enable_rounds!=2 || !frame)return 0;
    if(frame->physical_inputs>1 || (frame->physical_inputs && !match->config->camera))return 0;
    T3_SIM_START(copy_in,T3_SIM_COPY);
    n=*match;
    T3_SIM_END(copy_in);
    /* External effect storage commits with the atomic match; the native
     * in-place API avoids this copy and treats any failure as fatal. */
    if(match->config->effects) {
        t3_psx_effect_pool_t staged=*match->config->effects;
        t3_psx_match_config_t config=*match->config;
        config.effects=&staged;n.config=&config;
        if(!step_original(&n,p0,p1,frame))return 0;
        *match->config->effects=staged;n.config=match->config;
    } else if(!step_original(&n,p0,p1,frame))return 0;
    T3_SIM_START(copy_out,T3_SIM_COPY);
    *match=n;
    T3_SIM_END(copy_out);return 1;
}
int t3_psx_match_step_original_in_place(t3_psx_match_t* match,t3_input_t p0,t3_input_t p1,
                                        const t3_psx_match_frame_t* frame) {
    if(!match || !match->config || match->config->enable_rounds!=2 || !frame)return 0;
    T3_SIM_START(step,T3_SIM_STEP);
    const int stepped=step_original(match,p0,p1,frame);
    T3_SIM_END(step);
    return stepped;
}
T3_SIM_WRAP(int,t3_psx_match_step_original,T3_SIM_MATCH,
    (t3_psx_match_t* match,t3_input_t p0,t3_input_t p1,
                               const t3_psx_match_frame_t* frame),
    (match,p0,p1,frame))
