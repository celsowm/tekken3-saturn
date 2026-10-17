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
static int replay_playing(const t3_psx_match_t* m) {
    return m->config->enable_rounds==2 && m->replay.mode>=1 && m->replay.mode<=4;
}

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
       t3_motion_sample_cached(c->sample_cache,&stream,frame,&c->spline,pose)!=1 ||
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
        facts.row=blend_row(c->bank,replay_playing(match)?v->root_slot:v->slot);
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
        facts.playback=(uint8_t)replay_playing(match);
        if(c->enable_rounds==2 && match->flow.blend_hold>0 &&
           (!facts.playback || !(match->replay.play&1))) {
            facts.global_hold=1;--match->flow.blend_hold;
        }
        if((!facts.playback || !(match->replay.play&1)) &&
           !t3_psx_blend_prepare(&f->blend,&facts,blend_sample,match))return 0;
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
       t3_motion_sample_cached(c->sample_cache,stream,(unsigned)f->vm.clock.pose_frame-1u,&c->spline,f->pose)!=1)return 0;
    /* FK/IK consumes the pose stream's own root channels. World placement
     * separately uses root_move, which can differ during transition4/5. */
    f->pose_frames=stream->frames;
    if(!t3_psx_pose_fk(f->pose,&c->ground_tables.trig,slots,&fk) ||
       t3_psx_pose_ik(f->pose,&c->ik,slots,fk,complete,&mask)!=1 ||
       !t3_psx_pose_world_root(f->pose,c->scale_percent,f->ground.anchor,f->ground.facing,
                               f->ground.heading,&c->ground_tables.trig,&root))return 0;
    root.origin=f->ground.root;
    if(c->rsqrt.data && !t3_psx_blend_locals(&f->blend,&c->rsqrt,complete))return 0;
    if(c->enable_rounds==2 && match->flow.camera!=2)f->head.active=0;
    else if(f->parts_mask) {
        t3_psx_body_tables_t tables={c->ground_tables,c->body_tables,384u};
        if(!t3_psx_head_look(&f->head,&tables,f->world,match->fighter[1u-index].world,
                            &f->vm.move,f->vm.clock.pose_frame,f->no_look_at,&complete[2]))return 0;
    }
    if(!t3_psx_compose_parts(parts,complete,mask,&root,f->world,&f->parts_mask) ||
       (f->parts_mask&0x3FFFFu)!=0x3FFFFu ||
       !t3_psx_parts_to_slots(parts,f->world,f->parts_mask,f->world,&f->parts_mask) ||
       (f->parts_mask&0x3FFFFu)!=0x3FFFFu)return 0;
    f->fk_mask=fk;f->ik_mask=mask&~fk;f->fk_root_xx=slots[0].m[0][0];
    if(replay_playing(match))return 1;
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
    if((c->enable_rounds!=2 || match->flow.camera<3) &&
       !t3_psx_body_separate(pair,match->fighter[0].ground.distance,&tables))return 0;
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
static void replay_store(const t3_psx_fighter_t* f,t3_psx_replay_frame_t* r) {
    *r=(t3_psx_replay_frame_t){0};
    r->slot=f->vm.slot;r->src_row=f->blend.src_row;r->dst_row=f->blend.dst_row;
    r->root=f->ground.root;r->anchor=f->ground.anchor;r->heading=f->ground.heading;
    r->src_frame=f->blend.src_frame;r->dst_frame=f->blend.dst_frame;
    r->pose_frame=(uint8_t)f->vm.clock.pose_frame;r->blend_frames=(uint8_t)f->blend.frames;
    r->blend_bits=(uint8_t)((f->blend.mode<<4)|(f->blend.active&15));
    r->power=(uint8_t)f->ground.power_timer;
}
int t3_psx_match_init(t3_psx_match_t* out,const t3_psx_match_config_t* config) {
    t3_psx_match_t n={0};
    if(!out || !config || !config->bank || !config->ground_tables.atan ||
       config->ground_tables.atan_size!=1025u || !config->ground_tables.trig.data ||
       config->ground_tables.trig.size!=T3_PSX_TRIG_BYTES ||
       config->max_health_q16<=0 || config->scale_percent<1u || config->scale_percent>200u ||
       config->enable_rounds>2)return 0;
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
    for(unsigned i=0;i<2;++i)n.round_actor[i].active=1;
    if(!reset_fighters(&n))return 0;
    if(config->enable_rounds==2 && config->replay_storage)config->replay_storage->epoch=n.replay.epoch;
    *out=n;return 1;
}
static int advance_core(t3_psx_match_t* n,t3_input_t p0,t3_input_t p1,t3_psx_replay_frame_t record[2]) {
    const t3_psx_match_config_t* c;
    t3_input_t pads[2]={p0,p1};
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
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t* f=&n->fighter[i];
        f->previous_anchor=f->ground.anchor;
        if(c->enable_rounds==2 && !f->body.active)continue;
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
            if(decoded<0 || !t3_psx_ground_start(&f->ground,&n->fighter[1u-i].ground,
                    &f->vm,&before,&c->ground_tables,flags,
                    (uint8_t)(f->vm.current_branch_kind==3u || f->vm.current_branch_kind==4u),
                    facts,&f->reaction,count))return 0;
        }
    }
    if(!pair_distance(n))return 0;
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t* f=&n->fighter[i];
        const t3_command_frame_t input=t3_history_at(&f->input,0);
        if(!t3_psx_ground_step(&f->ground,&n->fighter[1u-i].ground,&f->vm,
                &n->fighter[1u-i].vm,&c->ground_tables,input.direction,input.pressed_buttons))return 0;
    }
    for(unsigned i=0;i<2;++i) {
        n->fighter[i].body.previous_root=n->fighter[i].ground.root;
        n->fighter[i].body.placed=n->fighter[i].ground.root;
        if(c->enable_rounds!=2 || n->fighter[i].body.active) {
            if(!place(n,&n->fighter[i]))return 0;
            if(c->enable_body && !compose(n,i))return 0;
            if(record)replay_store(&n->fighter[i],&record[i]);
        }
    }
    if(c->enable_body && (!bodies(n) || !contacts(n)))return 0;
    if(c->enable_combat) {
        for(unsigned i=0;i<2;++i) {
            t3_psx_fighter_t* f=&n->fighter[i];
            if(t3_psx_move_events_sample(f->vm.active_bank,&f->vm.move,f->previous_pose_frame,
                                         f->vm.clock.pose_frame,&f->events)!=1)return 0;
        }
        if(c->enable_rounds==2?!n->flow.no_damage:!c->enable_rounds || !n->round.done) {
            int32_t hp[2]={n->fighter[0].health_q16,n->fighter[1].health_q16};
            int16_t pose[2]={n->fighter[0].vm.clock.pose_frame,n->fighter[1].vm.clock.pose_frame};
            if(!t3_psx_match_apply_hits(n))return 0;
            if(c->enable_rounds==2 && n->replay.mode==7 && n->replay.mark<0) {
                for(unsigned i=0;i<2;++i)if(hp[i]>0 && !n->fighter[i].health_q16) {
                    if(!t3_psx_replay_mark_ko(&n->replay,(unsigned)pose[1u-i]))return 0;
                    break;
                }
            }
        } else for(unsigned i=0;i<2;++i) {
            n->fighter[i].hit=(t3_psx_hit_apply_result_t){0};
            n->fighter[i].got_hit=0;n->fighter[i].hit_freeze_in=0;
            n->fighter[i].ground.hit_clean=0;n->fighter[i].ground.last_damage=0;
        }
    }
    /* Source input runs AFTER physics, not before the current ground step. */
    for(unsigned i=0;i<2;++i) {
        if(pads[i].held&T3_PSX_INPUT_CLEAR) {
            t3_history_clear(&n->fighter[i].input);
            if(c->enable_rounds==2)n->round_actor[i].script_input=0;
        }
        else t3_history_push(&n->fighter[i].input,pads[i]);
    }
    for(unsigned j=0;j<2;++j) {
        unsigned i=(j+(n->ticks&1u))&1u;
        t3_psx_fighter_t *f=&n->fighter[i],*opp=&n->fighter[1u-i];
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
    if(c->enable_rounds==1 && !n->round.done) {
        const int32_t hp[2]={n->fighter[0].health_q16,n->fighter[1].health_q16};
        (void)t3_psx_round_step(&n->round,hp,c->max_health_q16);
    }
    ++n->ticks;return 1;
}
int t3_psx_match_step(t3_psx_match_t* match,t3_input_t p0,t3_input_t p1) {
    if(!match || !match->config || match->config->enable_rounds==2)return 0;
    t3_psx_match_t n=*match;
    if(!advance_core(&n,p0,p1,0))return 0;
    *match=n;return 1;
}
/* Normal VS's later-round FighterPlace. Records persist across rounds; a
 * fighter inactive at placement keeps its pending stance until MoveStartAll. */
static int next_original_round(t3_psx_match_t* n) {
    t3_psx_round_flow_t* r=&n->flow;
    if(r->match_result || r->played>=9)return 0;
    ++r->played;r->frame=r->counter=0;r->timer=2400;r->no_damage=0;
    r->camera=r->freeze=r->input[0]=r->input[1]=r->result=0;
    r->state=T3_ROUND_INTRO;r->win_done[0]=r->win_done[1]=0;r->blend_hold=20;
    if(n->replay.epoch==UINT32_MAX)return 0;
    t3_psx_replay_reset(&n->replay,n->replay.epoch+1);
    n->replay_skip=0;n->intro_camera_left=53;
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t* f=&n->fighter[i];t3_psx_ground_state_t* g=&f->ground;
        t3_move_vm_t* v=&f->vm;
        t3_history_clear(&f->input);n->round_actor[i].script_input=0;
        f->blend.mode=0;
        g->distance=1000;f->body.placed=g->anchor;
        v->pending_valid=0;
        if(!t3_move_vm_queue(v,1,0,v->current_branch_kind,1,-1))return 0;
        v->move=v->root_move=v->pending_move;
        v->active_bank=v->root_bank=v->pending_bank;v->root_slot=1;
        v->clock.pose_frame=v->clock.root_frame=1;v->last_entry_frame=1;
        if(f->body.active) {
            t3_move_start_facts_t facts={f->got_hit,(uint8_t)(f->health_q16!=0),f->hit_freeze_in};
            if(t3_move_vm_begin(v,facts)!=1 || !t3_psx_ground_start(g,&n->fighter[1u-i].ground,v,&v->move,
                &n->config->ground_tables,0,0,facts,&f->reaction,0))return 0;
        }
        v->clock.pose_frame=v->clock.root_frame=1;
        g->anchor=(t3_vec3i_t){i?1000:-1000,0,0};
        g->root.x=g->anchor.x;g->root.z=0;
        g->heading=g->facing=g->relative_angle=(uint16_t)(i?0xC000:0x4000);
        f->body.push=(t3_vec3i_t){0,0,0};
        f->world[2].origin=(t3_vec3i_t){g->anchor.x,-0x591,0};
        f->world[1].origin=(t3_vec3i_t){g->anchor.x,-0x41B,0};
        f->health_q16=n->config->max_health_q16;
        v->clock.hold_frames=-1;v->clock.hit_freeze=v->clock.ballistic=0;
        g->slide_state=g->was_hit_this_move=g->air_phase=g->in_air=0;
        g->juggle_count=g->hit_cooldown=g->push_frames=g->push_table_frames=0;
        g->turn_frames=g->turn_step=g->recover_mash=g->recover_frames=0;
        g->push_repeat=g->push_repeat_timer=g->power_timer=g->last_damage=0;
        g->anchor_dirty=g->slide_to_point=g->fixed_facing=0;g->throw_state=0;
        f->blend_guarded_previous=f->blend_in_reaction=0;
        f->hit.guarded=f->hit.hit_clean=0;f->ko_collapse=0;
        f->contact=f->whiffed=f->got_hit=0;f->hit_freeze_in=0;
        /* Port diagnostics count successful contacts per round. */
        f->registered_hits=f->registered_blocks=0;
        f->body.active=1;f->body.invulnerable=0;f->no_look_at=0;
        f->blend.frames=f->blend.counter=0;f->blend.weight=0;
        for(unsigned k=0;k<3;++k)f->blend.root_delta[k]=0;
        n->round_actor[i].health_left=n->round_actor[i].won=n->round_actor[i].win_pose=0;
        f->head.current[0]=f->head.current[2]=0;f->head.current[1]=0x38E;
        f->head.active=0;f->head.snap=1;
    }
    return pair_distance(n);
}
static int replay_apply_pose(t3_psx_match_t* n,unsigned i,const t3_psx_replay_frame_t* r) {
    t3_psx_fighter_t* f=&n->fighter[i];t3_move_record_t move;const t3_divmot_t* owner;
    if(r->tilt_x || r->tilt_z || !r->pose_frame || r->slot>=T3_DIVMOT_MOVE_SLOTS ||
       !t3_move_vm_resolve(n->config->bank,n->config->common,r->slot,&move,&owner))return 0;
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
        for(unsigned i=0;i<2;++i)replay_store(&n->fighter[i],&n->replay.saved[i]);
    for(unsigned i=0;i<2;++i) {
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
int t3_psx_match_step_original(t3_psx_match_t* match,t3_input_t p0,t3_input_t p1,
                               const t3_psx_match_frame_t* frame) {
    t3_psx_match_t n;
    t3_psx_round_flow_facts_t facts={0};
    t3_psx_round_flow_actions_t actions;
    t3_psx_replay_frame_t record[2]={{0}};
    if(!match || !match->config || match->config->enable_rounds!=2 || !frame)return 0;
    n=*match;
    if(n.config->replay_storage && n.config->replay_storage->epoch!=n.replay.epoch)return 0;
    if(n.flow.state==T3_ROUND_END && n.flow.match_result) {
        /* Port result screen: preserve the terminal fight while its input
         * event clock continues. Original FightMain has left FightFrame. */
        if(n.ticks==UINT32_MAX)return 0;
        ++n.ticks;*match=n;return 1;
    }
    if(n.flow.state==T3_ROUND_END && !next_original_round(&n))return 0;
    if(n.flow.replay_request) {
        if(!t3_psx_replay_start(&n.replay))return 0;
        n.flow.replay_request=0;
        if(n.replay.mode==T3_REPLAY_STARTING)n.flow.blend_hold=20;
    }
    int ending=n.replay.mode==T3_REPLAY_ENDING;
    int playback=t3_psx_replay_update(&n.replay);
    if(playback<0)return 0;
    if(ending) {
        n.flow.blend_hold=20;
        for(unsigned i=0;i<2;++i) {
            n.fighter[i].blend.frames=n.fighter[i].blend.counter=0;n.fighter[i].blend.weight=0;
            for(unsigned k=0;k<3;++k)n.fighter[i].blend.root_delta[k]=0;
        }
    }
    int old_camera=n.flow.camera;uint8_t old_active[2]={n.fighter[0].body.active,n.fighter[1].body.active};
    facts.replay_mode=n.replay.mode;facts.replay_skip=n.replay_skip;facts.frame_counter=frame->frame_counter;
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t* f=&n.fighter[i];t3_psx_round_actor_t* a=&n.round_actor[i];
        a->health=f->health_q16;a->maximum=n.config->max_health_q16;
        a->active=f->body.active;a->pose=f->vm.clock.pose_frame;a->length=f->vm.move.length;
        a->bank_type=n.config->bank->bank_type;a->is_cpu=f->ground.is_cpu;
        if(f->vm.pending_valid)a->state_word=(int32_t)f->vm.pending_move.state_word;
        else if(n.flow.state==T3_ROUND_RESULT && a->health && a->won>=0 &&
                ((n.flow.result&1) || a->is_cpu) && a->bank_type!=18) {
            t3_move_record_t stance;const t3_divmot_t* owner;
            if(!t3_move_vm_resolve(n.config->bank,n.config->common,f->vm.move.stance_slot,&stance,&owner))return 0;
            a->state_word=(int32_t)stance.state_word;
        } else a->state_word=0;
        facts.physical[i]=frame->physical[i];facts.pressed[i]=frame->pressed[i];
    }
    if(!t3_psx_round_flow_step(&n.flow,n.round_actor,&facts,&actions))return 0;
    if(actions.fight_clock_advance)++n.fight_clock;
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t* f=&n.fighter[i];
        f->body.active=(uint8_t)n.round_actor[i].active;
        if(actions.start_slot[i]>=0) {
            t3_move_start_facts_t start={f->got_hit,(uint8_t)(f->health_q16!=0),f->hit_freeze_in};
            f->blend.mode=0;
            if(actions.grounded_mask&(1u<<i))f->ground.anchor.y=0;
            f->vm.pending_valid=0;f->vm.clock.pose_frame=1;
            if(!t3_move_vm_queue(&f->vm,(unsigned)actions.start_slot[i],6,f->vm.current_branch_kind,1,-1) ||
               t3_move_vm_begin(&f->vm,start)!=1 ||
               /* RoundFlow binds pose_move before MoveStartOrAdvance. Its
                * reanchor/power tail reads the new row, unlike a queued move. */
               !t3_psx_ground_start(&f->ground,&n.fighter[1u-i].ground,&f->vm,&f->vm.move,
                   &n.config->ground_tables,0,0,start,&f->reaction,0))return 0;
            f->hit_done=0;
        }
    }
    if(actions.animate_before_camera) {
        int camera=n.flow.camera,hold=n.flow.blend_hold;
        n.flow.camera=old_camera;n.flow.blend_hold=20;
        for(unsigned i=0;i<2;++i) {
            n.fighter[i].body.active=old_active[i];
            if(old_active[i] && (!place(&n,&n.fighter[i]) ||
                (n.config->enable_body && !compose(&n,i))))return 0;
        }
        n.flow.camera=camera;n.flow.blend_hold=hold;
        for(unsigned i=0;i<2;++i)n.fighter[i].body.active=(uint8_t)n.round_actor[i].active;
    }
    if(actions.clear_blends)for(unsigned i=0;i<2;++i) {
            t3_psx_fighter_t* f=&n.fighter[i];
            f->blend.frames=f->blend.counter=0;f->blend.weight=0;
            for(unsigned k=0;k<3;++k)f->blend.root_delta[k]=0;
    }
    if(playback) {
        for(unsigned i=0;i<2;++i)if((n.flow.human_mask&(1u<<i)) && (frame->pressed[i]&0x800u))
            n.replay.skip=n.replay_skip=1;
        if(n.ticks==UINT32_MAX || !replay_render(&n))return 0;
        ++n.ticks;*match=n;return 1;
    }
    /* Original camera setup advances winner phase 3/4 to phase 5 during the
     * following live frame; exact view/projection is a separate owner. */
    if(n.flow.camera==3 || n.flow.camera==4)n.flow.camera=5;
    else if(n.flow.camera<2 && n.intro_camera_left>0) {
        n.flow.camera=--n.intro_camera_left?1:2;
    }
    t3_input_t pads[2]={p0,p1};
    for(unsigned i=0;i<2;++i) {
        if(!n.flow.input[i])pads[i]=(t3_input_t){T3_PSX_INPUT_CLEAR,0};
        else if(n.flow.input[i]==2)pads[i]=(t3_input_t){
            (uint16_t)(n.round_actor[i].script_input&0x1000?T3_IN_LEFT:0),0};
    }
    if(!advance_core(&n,pads[0],pads[1],n.replay.mode==7?record:0))return 0;
    if(n.config->replay_storage) {
        t3_psx_replay_storage_t* storage=n.config->replay_storage;
        storage->epoch=n.replay.epoch;
        if(n.replay.mode==7) {
            t3_psx_replay_pair_t* slot=&storage->ring[n.replay.record];
            slot->actor[0]=record[0];slot->actor[1]=record[1];slot->epoch=n.replay.epoch;
        }
    }
    *match=n;return 1;
}
