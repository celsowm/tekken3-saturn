/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Integration of pinned Tekken3Remake HitApply, HitCopySlotFlags and grounded
 * HitSelectReaction/MoveStep order. Changes: typed pair-atomic state and
 * sparse private reaction records. No prototype damage or timing policy.
 * Includes immediate USA VS KO collapse. Air reactions, reversals and throw
 * pairing remain unsupported by this bounded transaction.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "sim_profile.h"
#include "psx_match.h"

typedef struct {
    t3_move_vm_t vm;
    t3_psx_ground_state_t ground;
    t3_psx_hit_apply_result_t hit;
    uint8_t got_hit,collapsed;
    int16_t freeze;
} staged_t;
static int16_t abs_damage(int16_t n) {
    int32_t v=n<0?-(int32_t)n:n;
    return (int16_t)(v<32768?v:v-65536);
}
int T3_SIM_IMPL(t3_psx_match_apply_hits)(t3_psx_match_t* match) {
    staged_t staged[2];
    const t3_psx_match_config_t* c;
    if(!match || !match->config || !match->config->enable_combat)return 0;
    c=match->config;
    for(unsigned i=0;i<2;++i) {
        const unsigned a=1u-i;
        const t3_psx_fighter_t *f=&match->fighter[i],*attacker=&match->fighter[a];
        const t3_psx_hit_test_gate_t* gate=&attacker->hit_gate;
        t3_psx_hit_apply_frame_t frame={0};t3_psx_hit_facts_t facts={0};
        staged_t* s=&staged[i];
        s->vm=f->vm;s->ground=f->ground;s->got_hit=0;s->collapsed=0;s->freeze=0;
        frame.health_q16=f->health_q16;frame.max_health_q16=c->max_health_q16;
        frame.defender_extra=(t3_psx_hit_extra_t){f->events.extra_kind,f->events.extra_damage,f->ground.damage,f->ground.throw_state};
        frame.opponent_extra=(t3_psx_hit_extra_t){attacker->events.extra_kind,attacker->events.extra_damage,attacker->ground.damage,attacker->ground.throw_state};
        if(gate->status==T3_HIT_GATE_ACCEPT) {
            if(gate->hit_slot<0 || gate->hit_slot>1 || gate->zone<0 || gate->zone>=14)return 0;
            s->ground.fixed_facing=0;
            if(!t3_psx_ground_angles(&s->ground,&attacker->ground,&c->ground_tables))return 0;
            facts.attacker_damage=attacker->ground.damage;facts.defender_damage=f->ground.damage;
            facts.defender_guard=f->ground.guard;facts.defender_rel_angle=s->ground.relative_angle;
            facts.defender_pose_frame=f->vm.clock.pose_frame;facts.defender_active_first=f->vm.move.active_first;
            facts.defender_state=(uint16_t)f->vm.move.state_word;
            facts.juggle_count=(uint16_t)f->ground.juggle_count;
            /* The close reaction reads g_fighterDistance (fighter 0's). */
            facts.fighter_distance=match->fighter_distance;
            facts.attacker_power=attacker->ground.power_timer!=0;facts.defender_power=f->ground.power_timer!=0;
            facts.chip_enabled=c->chip_enabled;facts.defender_in_air=f->ground.in_air!=0;
            {
                /* HitClassify: the close entry of the attacking move (+0x34). */
                const size_t close=4u*attacker->vm.move.close_reaction;
                if(attacker->vm.move.close_reaction && c->close_reactions && close+4u<=c->close_reactions_size) {
                    const uint8_t* p=c->close_reactions+close;
                    facts.close_reaction_word=(uint16_t)(p[0]|(p[1]<<8));
                    facts.close_threshold=(uint32_t)(int32_t)(int16_t)(uint16_t)(p[2]|(p[3]<<8));
                    facts.close_table_available=1;
                }
            }
            frame.slots[(unsigned)gate->hit_slot]=(t3_psx_hit_slot_input_t){1,(uint8_t)a,(uint8_t)gate->zone,&attacker->vm.move,&facts};
            s->got_hit=1;s->freeze=gate->hit_freeze_in;
            s->ground.hit_cooldown=gate->hit_cooldown;s->ground.was_hit_this_move=1;
        }
        if(!t3_psx_hit_apply_preview(&frame,&s->hit))return 0;
        s->ground.last_damage=abs_damage(s->hit.slot_damage);
        s->ground.hit_clean=s->hit.hit_clean;
        if(s->got_hit) {
            const t3_psx_hit_candidate_t* best=&s->hit.slots[(unsigned)s->hit.best_slot];
            t3_psx_reaction_facts_t reaction_facts={0};
            reaction_facts.defender_state=(uint16_t)s->ground.state;
            reaction_facts.facing_quadrant=s->ground.facing_quadrant;reaction_facts.in_air=s->ground.in_air;
            /* HitApply copies the attacker's AttackVelocity (0x80040CF8):
             * its first attack segment's end minus its start. */
            {
                const t3_world_segment_t* g=&attacker->strike.segments[0];
                s->ground.hit_dir=(t3_vec3i_t){(int32_t)((uint32_t)g->end.x-(uint32_t)g->start.x),
                    (int32_t)((uint32_t)g->end.y-(uint32_t)g->start.y),(int32_t)((uint32_t)g->end.z-(uint32_t)g->start.z)};
            }
            reaction_facts.defender_slot=f->vm.slot;reaction_facts.defender_pose_frame=f->vm.clock.pose_frame;
            reaction_facts.defender_root_y=s->ground.root.y;reaction_facts.hit_dir_y=s->ground.hit_dir.y;
            reaction_facts.attacker_bank=attacker->vm.bank?attacker->vm.bank->bank_type:0u;
            reaction_facts.attacker_slot=attacker->vm.slot;
            reaction_facts.attacker_pending_slot=attacker->vm.pending_valid?(int32_t)attacker->vm.pending_slot:-1;
            if(!t3_psx_reaction_select_sparse(best,&reaction_facts,c->reactions,c->reactions_size,
                                              c->ground_tables.air_kinds,&s->hit.reaction))return 0;
            if(s->hit.reaction.air) {
                /* AirReactionKind writes the kind and ground offset at once. */
                s->ground.air_kind=s->hit.reaction.air_kind;
                s->ground.ground_offset=s->hit.reaction.ground_offset;
            }
            s->hit.reaction_known=1;
            if(s->hit.reaction.selected) {
                /* A reaction replaces an already queued input/continuation;
                 * MoveStep leaves the last branch window (+0x6C) in place. */
                s->hit.prior_pending_valid=s->vm.pending_valid;s->hit.prior_pending_slot=s->vm.pending_slot;
                s->vm.pending_valid=0;
                if(!t3_move_vm_queue(&s->vm,s->hit.reaction.move_slot,s->hit.reaction.transition,
                                     1u,f->vm.clock.pose_frame,s->vm.branch_window))return 0;
            }
        }
    }
    /* USA VS FighterCollapse runs immediately in MoveStepAll on a clean KO,
     * after selecting the ordinary reaction. It does not wait for next tick. */
    for(unsigned i=0;i<2;++i) {
        staged_t* s=&staged[i];
        if(s->hit.health_q16==0 && s->hit.hit_clean) {
            const t3_move_record_t* next=s->vm.pending_valid?&s->vm.pending_move:&s->vm.move;
            int collapse=next->stance_slot==3u || next->stance_slot==40u ||
                         (!(next->state_word&4u) && !next->air_start);
            if(!c->collapse_on_ko)return 0; /* the alternate airborne KO is not yet ported */
            if(collapse) {
                t3_move_record_t previous=s->vm.move;
                const t3_move_start_facts_t facts={s->got_hit,0,s->freeze,
                                                   (int16_t)s->vm.clock.pose_frame};
                s->hit.collapsed=1;s->hit.prior_slot=s->vm.slot;
                s->hit.prior_pose_frame=(uint16_t)s->vm.clock.pose_frame;s->hit.prior_attack_word=s->vm.move.attack_word;
                s->vm.pending_valid=0;s->vm.clock.pose_frame=1;
                if(!t3_move_vm_queue(&s->vm,0xD66u,6u,1u,1,-1) ||
                   t3_move_vm_begin(&s->vm,facts)!=1)return 0;
                s->hit.guarded=0;s->hit.hit_clean=1;s->ground.guard=0;
                if(!t3_psx_ground_start(&s->ground,&staged[1u-i].ground,&s->vm,&previous,
                        &c->ground_tables,0,0,facts,&s->hit.reaction,0))return 0;
                s->collapsed=1;
            }
        }
    }
    /* Publish only after both actors, including their destination rows, pass. */
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t* f=&match->fighter[i];const staged_t* s=&staged[i];
        f->vm=s->vm;f->ground=s->ground;f->hit=s->hit;
        f->health_q16=s->hit.health_q16;f->got_hit=s->got_hit;f->hit_freeze_in=s->freeze;
        if(s->got_hit && s->hit.reaction.selected)f->reaction=s->hit.reaction;
        /* FightFrame clears +0xCF/+0xD3 immediately before HitApply. A hit
         * writes this frame's outcome; without one the flags stay clear. */
        f->last_guarded=s->hit.guarded;f->last_counter=s->hit.counter;
        if(s->got_hit){if(s->hit.guarded)++f->registered_blocks;else ++f->registered_hits;}
        if(s->collapsed)f->last_guarded=0;
        f->contact=f->hit_gate.status==T3_HIT_GATE_ACCEPT;
        if(f->contact){f->hit_done=1;f->contact_this_move=1;}
        if(s->collapsed){f->ko_collapse=1;f->hit_done=0;f->started=1;
            f->blend_guarded_previous=0;f->blend_in_reaction=1;}
        f->whiffed=(uint8_t)(f->vm.clock.pose_frame==f->vm.move.active_last && !f->contact_this_move);
    }
    return 1;
}
T3_SIM_WRAP(int,t3_psx_match_apply_hits,T3_SIM_HITS,
    (t3_psx_match_t* match),
    (match))
