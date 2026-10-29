/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * MoveReactionBranch and FighterOpponent adapted from pinned Tekken3Remake
 * move_system.gd / fight_state.gd. Changes: owned C match state and bounded
 * private tables; the outer match step owns atomic publication.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "psx_match.h"
#include "psx_reversal.h"
#include "attack_records.h"

typedef struct {
    const t3_psx_match_t* match;
    const t3_psx_fighter_t* self;
    const t3_psx_fighter_t* opponent;
    int failed;
} query_t;
static int list_query(void* user,int16_t list) {
    query_t* q=user;
    t3_motion_stream_t stream;
    const t3_psx_match_config_t* c=q->match->config;
    const t3_move_vm_t* v=&q->opponent->vm;
    if(t3_motion_open_header(v->active_bank,c->common,v->move.animation_offset,&stream)!=1) {
        q->failed=1;return 0;
    }
    const int found=t3_psx_reversal_list_has(c->reversal_lists,c->reversal_lists_size,list,stream.tag);
    if(found<0){q->failed=1;return 0;}
    return found;
}
static int condition(void* user,const t3_branch_record_t* row) {
    query_t* q=user;
    const t3_psx_fighter_t *f=q->self,*o=q->opponent;
    t3_psx_reversal_facts_t facts={
        (int16_t)f->ground.relative_angle,(int16_t)o->ground.relative_angle,
        (int16_t)o->vm.move.attack_word,(int16_t)o->vm.slot,f->got_hit,{0},o->vm.move.flags,list_query,q};
    if(row->condition>=0x46u && row->condition<=0x49u && row->parameter &&
       t3_psx_reversal_condition(row->condition,0,&facts) &&
       t3_attack_descriptor_joints(o->vm.active_bank,&o->vm.move,facts.joints)!=1)return -1;
    const int matched=t3_psx_reversal_condition(row->condition,row->parameter,&facts);
    return q->failed?-1:matched;
}
static unsigned opponent(const t3_psx_fighter_t* f,unsigned i) {
    if(f->ground.in_throw)return 1u-i;
    if(f->ground.was_hit_this_move)return f->last_attacker;
    if(f->contact)return f->last_hit_target;
    return 1u-i;
}
int t3_psx_match_reversals(t3_psx_match_t* n) {
    const t3_psx_match_config_t* c=n->config;
    n->reversal_cancelled=0;
    /* HitClearSlots and HitTest's writes, before selecting FighterOpponent. */
    for(unsigned i=0;i<2;++i) {
        t3_psx_fighter_t* f=&n->fighter[i];
        f->got_hit=0;f->contact=0;
    }
    for(unsigned i=0;i<2;++i)if(n->fighter[i].hit_gate.status==T3_HIT_GATE_ACCEPT) {
        t3_psx_fighter_t *a=&n->fighter[i],*d=&n->fighter[1u-i];
        a->contact=a->contact_this_move=a->hit_done=1;a->last_hit_target=(uint8_t)(1u-i);
        d->got_hit=1;d->ground.was_hit_this_move=1;
    }
    for(unsigned j=0;j<2;++j) {
        const unsigned i=(j+((n->frame_counter&1u)?0u:1u))&1u;
        t3_psx_fighter_t* f=&n->fighter[i];
        const unsigned oi=opponent(f,i);
        if(oi>1)return 0;
        t3_psx_fighter_t* o=&n->fighter[oi];
        if(c->character[oi]==17u)continue;
        f->ground.damage_override=0;
        if(!f->got_hit)continue;
        t3_branch_context_t restrictions;
        t3_psx_match_branch_facts(n,i,&restrictions);
        restrictions.opponent_bank=t3_psx_match_bank(c,oi)->bank_type;
        query_t query={n,f,o,0};t3_branch_record_t row;
        const int found=t3_psx_reversal_find(f->vm.active_bank,f->vm.move,&f->input,
            (unsigned)f->vm.clock.pose_frame,f->vm.bank->bank_type,&restrictions,
            c->sequences,c->sequence_count,condition,&query,&row);
        if(found<0)return 0;
        if(!found)continue;
        t3_move_record_t target;const t3_divmot_t* owner;
        if(!t3_move_vm_resolve(f->vm.bank,c->common,row.target_slot,&target,&owner))return 0;
        const unsigned kind=f->vm.pending_valid?f->vm.queued.branch_kind:f->vm.current_branch_kind;
        const unsigned transition=t3_move_transition_remap(row.flags&63u,&target,row.target_frame);
        const int16_t window=(int16_t)((int)row.last_frame-f->vm.clock.pose_frame);
        f->vm.pending_valid=0;
        if(!t3_move_vm_queue(&f->vm,row.target_slot,transition,kind,row.target_frame,window))return 0;
        f->vm.branch_window=window;
        f->ground.was_hit_this_move=0;f->got_hit=0;
        f->ground.hit_clean=0;f->ground.hit_cooldown=0;
        o->contact=o->contact_this_move=o->hit_done=0;
        /* Hit slots and the chosen opponent's contact flags are distinct:
         * FighterOpponent can even select f itself on the first contact. */
        n->reversal_cancelled|=(uint8_t)((1u<<i)|(1u<<(oi+2u)));
        f->ground.damage_override=(int16_t)((int16_t)o->ground.damage/2+25);
    }
    return 1;
}
