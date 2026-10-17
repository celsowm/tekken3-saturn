/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Original-state integration using the pinned Tekken3Remake frame sequence
 * and RootReanchor. Changes: caller-owned portable state/tables and atomic
 * failure; no prototype physics/health policy is called or copied.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "psx_match.h"
#include "attack_records.h"
#include "psx_contact.h"
#include "branch_rules.h"
#include "fixed_shift.h"

static uint32_t blend_row(const t3_divmot_t* bank,unsigned slot) {
    const uint8_t* p=bank->data+bank->sections[2]+4u*slot;
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static int blend_sample(void* user,uint32_t row,unsigned frame,int16_t root[3],t3_psx_matrix_t out[18]) {
    t3_psx_match_t* m=user;
    const t3_psx_match_config_t* c=m->config;
    const t3_divmot_t* bank=(row&0x40000000u)?c->common:c->bank;
    t3_move_record_t move;t3_motion_stream_t stream;
    int16_t pose[T3_PSX_POSE_CHANNELS];t3_psx_matrix_t fk[18];uint32_t mask;
    if(!t3_divmot_move(bank,row&0x3FFFFFFFu,&move) ||
       t3_motion_open(bank,c->common,move.animation_offset,&stream)!=1 ||
       t3_motion_sample(&stream,frame,&c->spline,pose)!=1 ||
       !t3_psx_pose_fk(pose,&c->ground_tables.trig,fk,&mask) ||
       t3_psx_pose_ik(pose,&c->ik,fk,mask,out,&mask)!=1)return 0;
    for(unsigned i=0;i<3;++i) {
        int32_t value=t3_floor_shift((int32_t)pose[i]*(int32_t)(c->scale_percent*4096u/100u),12);
        uint16_t bits=(uint16_t)value;
        root[i]=(int16_t)(bits<32768u?(int32_t)bits:(int32_t)bits-65536);
    }
    return 1;
}

static uint32_t isqrt(uint32_t n) {
    uint32_t guess=1,rest=n;
    if(!n)return 0;
    while(guess<rest){guess<<=1;rest>>=1;}
    for(;;){uint32_t before=guess;guess=(n/before+before)>>1;if(guess>=before)return before;}
}
static int pair_distance(t3_psx_match_t* match) {
    const int64_t x=(int64_t)match->fighter[1].ground.root.x-match->fighter[0].ground.root.x;
    const int64_t z=(int64_t)match->fighter[1].ground.root.z-match->fighter[0].ground.root.z;
    int64_t squared;
    if(x< -32767 || x>32767 || z< -32767 || z>32767)return 0;
    squared=x*x+z*z;
    match->fighter[0].ground.distance=match->fighter[1].ground.distance=isqrt((uint32_t)squared);
    return 1;
}
static int root_pose(const t3_psx_match_t* match,t3_psx_fighter_t* fighter,unsigned frame,int16_t root[3]) {
    const t3_motion_stream_t* stream;
    const t3_move_vm_t* vm=&fighter->vm;
    return t3_motion_open_cached(&fighter->root_cache,vm->root_bank,vm->common,
                                 vm->root_move.animation_offset,&stream)==1 &&
           t3_motion_sample_root(stream,frame,&match->config->spline,root)==1;
}
static int place(t3_psx_match_t* match,t3_psx_fighter_t* f) {
    int16_t xyz[3],pose[T3_PSX_POSE_CHANNELS]={0};
    t3_psx_joint_t result;
    const t3_psx_match_config_t* c=match->config;
    int32_t scale=(int32_t)(c->scale_percent*4096u/100u);
    if(f->vm.clock.root_frame<1 || f->vm.clock.pose_frame<1)return 0;
    if(f->ground.anchor_dirty) {
        int frame=f->vm.clock.root_frame-f->vm.clock.frame_step-1;
        const t3_vec3i_t zero={0,0,0};
        if(!root_pose(match,f,(unsigned)(frame<0?0:frame),xyz))return 0;
        pose[0]=xyz[0];pose[1]=xyz[1];pose[2]=xyz[2];
        if(!t3_psx_pose_world_root(pose,c->scale_percent,zero,f->ground.facing,f->ground.heading,
                                  &c->ground_tables.trig,&result))return 0;
        f->ground.anchor.x=f->ground.root.x-result.origin.x;
        f->ground.anchor.z=f->ground.root.z-result.origin.z;
        f->ground.anchor_dirty=0;
    }
    if(!root_pose(match,f,(unsigned)f->vm.clock.root_frame-1u,xyz))return 0;
    pose[0]=xyz[0];pose[1]=xyz[1];pose[2]=xyz[2];
    /* Paul's scale is 100%; keep the displayed root displacement explicit
     * for tracking modes that read its sign. */
    {
        int64_t x=(int64_t)xyz[0]*scale;
        int32_t scaled=(int32_t)(x>=0?x/4096:-((-x+4095)/4096));
        uint16_t bits=(uint16_t)scaled;
        f->ground.root_dx=(int16_t)(bits<32768u?(int32_t)bits:(int32_t)bits-65536);
    }
    if(!t3_psx_pose_world_root(pose,c->scale_percent,f->ground.anchor,f->ground.facing,
                              f->ground.heading,&c->ground_tables.trig,&result))return 0;
    if(c->rsqrt.data) {
        t3_psx_blend_facts_t facts={0};
        const t3_move_vm_t* v=&f->vm;
        facts.row=blend_row(c->bank,v->slot);
        facts.pending_row=v->pending_valid?blend_row(c->bank,v->pending_slot):0;
        if(v->move.stance_slot>=T3_DIVMOT_MOVE_SLOTS)return 0;
        facts.stance_row=blend_row(c->bank,v->move.stance_slot);
        facts.pose_frame=v->clock.pose_frame;facts.frame_step=v->clock.frame_step;
        facts.entry_frame=v->queued.entry_frame;facts.previous_event_frame=f->previous_pose_frame;
        facts.decoded_root_dy=xyz[1];facts.throw_state=f->ground.throw_state;
        facts.length=v->move.length;facts.active_first=v->move.active_first;facts.changed=v->clock.move_changed;
        facts.branch_kind=v->pending_valid?v->queued.branch_kind:v->current_branch_kind;
        facts.transition=v->queued.transition;facts.pending=v->pending_valid;
        facts.suppressed=(v->move.flags&0x100u)!=0 || f->blend_guarded_previous || f->blend_in_reaction;
        facts.no_forward=(v->move.flags&0x200u)!=0 || (v->pending_flags&64u)!=0 ||
            f->ground.juggle_count || (f->ground.in_air && f->ground.was_hit_this_move);
        if(!t3_psx_blend_prepare(&f->blend,&facts,blend_sample,match))return 0;
        result.origin.y=f->ground.anchor.y+t3_psx_blend_root_y(&f->blend,
            (int16_t)(result.origin.y-f->ground.anchor.y));
    }
    if(f->ground.air_phase==2u){result.origin.x=f->ground.anchor.x;result.origin.z=f->ground.anchor.z;}
    else if(f->ground.air_phase==1u)result.origin=f->ground.anchor;
    f->ground.root=result.origin;
    return 1;
}
static int compose(t3_psx_match_t* match,unsigned index) {
    t3_psx_fighter_t* f=&match->fighter[index];
    const t3_psx_match_config_t* c=match->config;
    const t3_psx_part_t* parts=c->parts[index];
    const t3_motion_stream_t* stream;
    t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],complete[T3_PSX_ROTATION_SLOTS];
    t3_psx_joint_t root;
    uint32_t fk=0,mask=0;
    if(t3_motion_open_cached(&f->pose_cache,f->vm.active_bank,f->vm.common,
                             f->vm.move.animation_offset,&stream)!=1 ||
       t3_motion_sample(stream,(unsigned)f->vm.clock.pose_frame-1u,&c->spline,f->pose)!=1)return 0;
    /* FK/IK consumes the pose stream's own root channels. World placement
     * separately uses root_move, which can differ during transition4/5. */
    f->pose_frames=stream->frames;
    if(!t3_psx_pose_fk(f->pose,&c->ground_tables.trig,slots,&fk) ||
       t3_psx_pose_ik(f->pose,&c->ik,slots,fk,complete,&mask)!=1 ||
       !t3_psx_pose_world_root(f->pose,c->scale_percent,f->ground.anchor,f->ground.facing,
                               f->ground.heading,&c->ground_tables.trig,&root))return 0;
    root.origin=f->ground.root;
    if(c->rsqrt.data && !t3_psx_blend_locals(&f->blend,&c->rsqrt,complete))return 0;
    if(f->parts_mask) {
        t3_psx_body_tables_t tables={c->ground_tables,c->body_tables,384u};
        if(!t3_psx_head_look(&f->head,&tables,f->world,match->fighter[1u-index].world,
                            &f->vm.move,f->vm.clock.pose_frame,f->no_look_at,&complete[2]))return 0;
    }
    if(!t3_psx_compose_parts(parts,complete,mask,&root,f->world,&f->parts_mask) ||
       (f->parts_mask&0x3FFFFu)!=0x3FFFFu ||
       !t3_psx_parts_to_slots(parts,f->world,f->parts_mask,f->world,&f->parts_mask) ||
       (f->parts_mask&0x3FFFFu)!=0x3FFFFu)return 0;
    f->fk_mask=fk;f->ik_mask=mask&~fk;f->fk_root_xx=slots[0].m[0][0];
    return t3_psx_body_shapes(&f->body,f->world,f->parts_mask,c->body_tables+384,
                 &f->vm.move,f->vm.clock.pose_frame,f->ground.in_air,f->ground.was_hit_this_move);
}
static int attack_shapes(t3_psx_match_t* match,t3_psx_fighter_t* f) {
    t3_attack_sample_t sample;t3_world_attack_t result;
    const t3_move_vm_t* vm=&f->vm;
    int decoded=t3_attack_sample(vm->active_bank,&vm->move,
        vm->move.active_first,0,&sample);
    if(decoded<0)return 0;
    if(!decoded)return f->ground.active_segments==0u;
    if(vm->clock.pose_frame>=vm->move.active_first && vm->clock.pose_frame<=vm->move.active_last) {
        decoded=t3_attack_sample(vm->active_bank,&vm->move,vm->clock.pose_frame,0,&sample);
        if(decoded!=1 || !t3_psx_attack_place(&sample,
                (unsigned)(vm->clock.pose_frame-vm->move.active_first),
                f->ground.anchor,f->previous_anchor,f->ground.root,f->ground.heading,f->ground.facing,
                &match->config->ground_tables.trig,&f->strike,&result))return 0;
        f->strike=result;
    } else {
        unsigned count=f->ground.attack_segment_count;
        if(count>sample.segment_count)return 0;
        for(unsigned k=0;k<count;++k) {
            unsigned a=sample.joint_a[k],b=sample.joint_b[k];
            if(a>=18u || b>=18u)return 0;
            f->strike.segments[k].end=f->world[a].origin;
            if(b)f->strike.segments[k].start=f->world[b].origin;
        }
        f->strike.segment_count=(uint8_t)count;
    }
    return 1;
}
static int contacts(t3_psx_match_t* match) {
    t3_hurt_cylinder_t hurt[2][T3_PSX_HURT_ZONES];
    for(unsigned i=0;i<2;++i)if(!t3_psx_hurt_from_parts(match->fighter[i].world,
                              match->fighter[i].parts_mask,0,hurt[i]))return 0;
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t *f=&match->fighter[i],*d=&match->fighter[1u-i];
        t3_psx_hit_test_facts_t facts={0};t3_world_attack_t active=f->strike;
        unsigned segment=0;int zone=-1;
        active.segment_count=f->ground.active_segments;
        if(active.segment_count>f->strike.segment_count)return 0;
        if(active.segment_count)zone=t3_psx_attack_contact(&active,hurt[1u-i],&segment);
        if(zone< -1)return 0;
        f->geometry=(t3_psx_shadow_result_t){active.segment_count?
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
        facts.defender_is_cpu=d->ground.is_cpu;facts.free_slots_mask=3;facts.game_mode=1;
        facts.hit_done=match->config->enable_combat?f->hit_done:0;
        if(!t3_psx_hit_test_gate(&facts,&f->hit_gate))return 0;
        if(f->hit_gate.status==T3_HIT_GATE_ACCEPT)++f->gate_frames;
    }
    return 1;
}
static int bodies(t3_psx_match_t* match) {
    const t3_psx_match_config_t* c=match->config;
    t3_psx_body_tables_t tables={c->ground_tables,c->body_tables,384u};
    t3_psx_body_actor_t pair[2];
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t* f=&match->fighter[i];
        f->body.anchor=f->ground.anchor;f->body.root=f->ground.root;
        f->body.facing=f->ground.facing;f->body.active_first=f->vm.move.active_first;
        f->body.distance=f->ground.distance;f->body.target_dir=f->ground.target_dir;
        pair[i]=f->body;
    }
    /* World joints/shapes remain those computed before separation, as in source. */
    for(unsigned i=0;i<2;++i) {
        if(!t3_psx_arena_bounds(&pair[i],&pair[1u-i],&tables))return 0;
        if(pair[i].ramp<2000) {
            pair[i].ramp+=match->fighter[i].ramp_step;
            match->fighter[i].ramp_step+=2;
            if(pair[i].ramp>2000){pair[i].ramp=2000;match->fighter[i].ramp_step=2;}
        }
        pair[i].contact=pair[i].contact_with=pair[i].no_push=0;
        if(match->fighter[i].ground.throw_state)return 0;
    }
    for(unsigned i=0;i<2;++i) {
        match->fighter[i].ground.anchor=pair[i].anchor;
        match->fighter[i].ground.root=pair[i].root;
        if(!attack_shapes(match,&match->fighter[i]))return 0;
    }
    if(!t3_psx_body_separate(pair,match->fighter[0].ground.distance,&tables))return 0;
    for(unsigned i=0;i<2;++i) {
        if(pair[i].contact)++match->fighter[i].body_contacts;
        match->fighter[i].body=pair[i];
        match->fighter[i].ground.anchor=pair[i].anchor;
        match->fighter[i].ground.root=pair[i].root;
    }
    return 1;
}
static int reset_fighters(t3_psx_match_t* n) {
    const t3_psx_match_config_t* config=n->config;
    for(unsigned i=0;i<2;++i)n->fighter[i]=(t3_psx_fighter_t){0};
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t* f=&n->fighter[i];
        if(!t3_move_vm_start_phased(&f->vm,config->bank,config->common,3u))return 0;
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
int t3_psx_match_init(t3_psx_match_t* out,const t3_psx_match_config_t* config) {
    t3_psx_match_t n={0};
    if(!out || !config || !config->bank || !config->ground_tables.atan ||
       config->ground_tables.atan_size!=1025u || !config->ground_tables.trig.data ||
       config->ground_tables.trig.size!=T3_PSX_TRIG_BYTES ||
       config->max_health_q16<=0 || config->scale_percent<1u || config->scale_percent>200u)return 0;
    if(config->enable_body && (!config->parts[0] || !config->parts[1] || !config->body_tables ||
       config->body_tables_size!=432u || !config->ik.ik_blob ||
       config->ik.ik_size!=T3_PSX_IK_BYTES))return 0;
    if(config->enable_combat && (!config->enable_body || !config->reactions ||
       !config->reactions_size || config->reactions_size%44u || config->chip_enabled>1u))return 0;
    if(config->rsqrt.data && (!config->enable_body || config->rsqrt.size!=T3_PSX_RSQRT_BYTES))return 0;
    n.config=config;
    t3_psx_round_init(&n.round);
    if(!reset_fighters(&n))return 0;
    *out=n;return 1;
}
int t3_psx_match_step(t3_psx_match_t* match,t3_input_t p0,t3_input_t p1) {
    t3_psx_match_t n;
    const t3_psx_match_config_t* c;
    t3_input_t pads[2]={p0,p1};
    if(!match || !match->config || match->ticks==UINT32_MAX)return 0;
    n=*match;c=n.config;
    if(c->enable_rounds && n.round.done) {
        const int32_t hp[2]={n.fighter[0].health_q16,n.fighter[1].health_q16};
        if(t3_psx_round_step(&n.round,hp,c->max_health_q16)) {
            if(!reset_fighters(&n))return 0;
        } else {
            /* Result keeps authored recovery/collapse running with no damage
             * and no new player commands. Match end remains latched for Start. */
            pads[0]=pads[1]=(t3_input_t){T3_PSX_INPUT_CLEAR,0};
        }
    }
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t* f=&n.fighter[i];
        f->previous_anchor=f->ground.anchor;
        t3_move_record_t before=f->vm.move;
        t3_move_start_facts_t facts={f->got_hit,(uint8_t)(f->health_q16!=0),f->hit_freeze_in};
        uint8_t flags=f->vm.pending_flags;
        f->previous_pose_frame=(uint16_t)f->vm.clock.pose_frame;
        int started=t3_move_vm_begin(&f->vm,facts);
        if(started<0)return 0;
        f->started=(uint8_t)started;
        if(started) {
            f->blend_guarded_previous=f->hit.guarded!=0;
            f->blend_in_reaction=f->hit.hit_clean!=0;
            f->hit_done=0;
            if(!f->contact)f->contact_this_move=0;
            t3_attack_sample_t attack;
            int decoded=t3_attack_sample(f->vm.active_bank,&f->vm.move,f->vm.move.active_first,0,&attack);
            uint8_t count=decoded==1?attack.segment_count:0;
            if(decoded<0 || !t3_psx_ground_start(&f->ground,&n.fighter[1u-i].ground,
                    &f->vm,&before,&c->ground_tables,flags,
                    (uint8_t)(f->vm.current_branch_kind==3u || f->vm.current_branch_kind==4u),
                    facts,&f->reaction,count))return 0;
        }
    }
    if(!pair_distance(&n))return 0;
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t* f=&n.fighter[i];
        const t3_command_frame_t input=t3_history_at(&f->input,0);
        if(!t3_psx_ground_step(&f->ground,&n.fighter[1u-i].ground,&f->vm,
                &n.fighter[1u-i].vm,&c->ground_tables,input.direction,input.pressed_buttons))return 0;
    }
    for(unsigned i=0;i<2;++i) {
        n.fighter[i].body.previous_root=n.fighter[i].ground.root;
        n.fighter[i].body.placed=n.fighter[i].ground.root;
        if(!place(&n,&n.fighter[i]))return 0;
        if(c->enable_body && !compose(&n,i))return 0;
    }
    if(c->enable_body && (!bodies(&n) || !contacts(&n)))return 0;
    if(c->enable_combat) {
        for(unsigned i=0;i<2;++i) {
            t3_psx_fighter_t* f=&n.fighter[i];
            if(t3_psx_move_events_sample(f->vm.active_bank,&f->vm.move,f->previous_pose_frame,
                                         f->vm.clock.pose_frame,&f->events)!=1)return 0;
        }
        if(!c->enable_rounds || !n.round.done) {
            if(!t3_psx_match_apply_hits(&n))return 0;
        } else for(unsigned i=0;i<2;++i) {
            n.fighter[i].hit=(t3_psx_hit_apply_result_t){0};
            n.fighter[i].got_hit=0;n.fighter[i].hit_freeze_in=0;
        }
    }
    /* Source input runs AFTER physics, not before the current ground step. */
    for(unsigned i=0;i<2;++i) {
        if(pads[i].held&T3_PSX_INPUT_CLEAR)t3_history_clear(&n.fighter[i].input);
        else t3_history_push(&n.fighter[i].input,pads[i]);
    }
    for(unsigned j=0;j<2;++j) {
        unsigned i=(j+(n.ticks&1u))&1u;
        t3_psx_fighter_t *f=&n.fighter[i],*opp=&n.fighter[1u-i];
        t3_branch_context_t facts={0};
        if(!t3_psx_ground_angles(&f->ground,&opp->ground,&c->ground_tables))return 0;
        if(c->enable_combat) {
            facts.known=T3_FACT_IDENTITY|T3_FACT_DISTANCE|T3_FACT_ORIENTATION|T3_FACT_RECOVERY|
                        T3_FACT_COLLISION|T3_FACT_CONTACT|T3_FACT_HEALTH|T3_FACT_POWER|T3_FACT_OPPONENT;
            facts.opponent_bank=0;facts.own_character=0;
            facts.distance=facts.adjusted_distance=(int32_t)f->ground.distance;
            facts.relative_angle=f->ground.relative_angle;facts.heading_delta=f->ground.heading_delta;
            facts.facing_quadrant=f->ground.facing_quadrant;facts.opponent_quadrant=f->ground.opponent_quadrant;
            facts.recover_frames=f->ground.recover_frames;facts.recover_mash=f->ground.recover_mash;
            facts.body_contact=f->body.contact;facts.contact=f->contact_this_move;
            facts.guarded=f->hit.guarded;facts.counter_hit=f->hit.counter;facts.whiffed=f->whiffed;
            facts.own_health=f->health_q16!=0;facts.own_power_timer=f->ground.power_timer;
            facts.own_attack=f->vm.move.attack_word;facts.opponent_attack=opp->vm.move.attack_word;
            facts.opponent_state=opp->ground.state;facts.opponent_damage=(uint16_t)opp->ground.damage;
            facts.opponent_frame=opp->vm.clock.pose_frame;facts.opponent_active_last=opp->vm.move.active_last;
            facts.opponent_air_start=opp->vm.move.air_start;facts.opponent_air_end=opp->vm.move.air_end;
            facts.opponent_transition=opp->vm.current_transition;
        }
        if(t3_move_vm_queue_input(&f->vm,&f->input,c->enable_combat?&facts:NULL,c->sequences,c->sequence_count)<0)return 0;
        if(c->enable_combat && f->vm.pending_valid && f->vm.queued.branch_kind!=1u) {
            unsigned code=f->vm.queued.transition,slot=f->vm.pending_slot;
            int unsupported=code==16u || code==18u || code==19u || code==28u ||
                            (code>=30u && code<=34u) || code==36u;
            if(f->vm.pending_move.active_first && slot!=295u && slot!=355u && slot!=357u)unsupported=1;
            if(unsupported){f->vm.pending_valid=0;++f->unsupported_moves;}
        }
    }
    if(c->enable_rounds && !n.round.done) {
        const int32_t hp[2]={n.fighter[0].health_q16,n.fighter[1].health_q16};
        (void)t3_psx_round_step(&n.round,hp,c->max_health_q16);
    }
    ++n.ticks;*match=n;return 1;
}
