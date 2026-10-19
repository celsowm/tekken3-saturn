/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of MoveStartOrAdvance, TransitionRemap and MoveBranchStep in
 * pinned Tekken3Remake tools/research/fight_sim.py and fight_math.py.
 * Changes: bounded normalized bank views, explicit begin/queue API, atomic
 * publication, unavailable-state errors and no fighter-physics side effects.
 * Look-ahead input branches use the supplied branch facts; original end-turn
 * orientation refresh, input-buffer mutation and throw/launch remain pending.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "sim_profile.h"
#include "move_vm.h"
#include "psx_motion.h"

int t3_move_vm_start_phased(t3_move_vm_t* vm,const t3_divmot_t* bank,
                            const t3_divmot_t* common,unsigned slot) {
    if(!t3_move_vm_start(vm,bank,common,slot))return 0;
    t3_move_clock_reset(&vm->clock,1);
    vm->phased=1u;vm->last_entry_frame=1;
    return 1;
}
static unsigned remap(unsigned code,const t3_move_record_t* m,int16_t entry) {
    const int enters=m->active_first && entry+1<m->active_last &&
                      m->active_last<=m->length;
    if(code==2u)return enters?20u:2u;
    if(code>=12u && code<=15u)return m->active_first?code:11u;
    if(code==20u || code==21u)return enters?code:11u;
    if(code==25u)return enters?22u:25u;
    return code;
}
int t3_move_vm_queue(t3_move_vm_t* vm,unsigned slot,unsigned transition,
                      unsigned kind,int16_t entry,int16_t window) {
    t3_move_record_t next;
    const t3_divmot_t* owner=NULL;
    if(!vm || !vm->phased || vm->pending_valid || transition>63u || kind>4u ||
       !t3_move_vm_resolve(vm->bank,vm->common,slot,&next,&owner))return 0;
    vm->pending_move=next;vm->pending_bank=owner;vm->pending_slot=(uint16_t)slot;
    vm->queued=(t3_move_start_t){0};
    vm->queued.transition=(uint8_t)transition;vm->queued.branch_kind=(uint8_t)kind;
    vm->queued.entry_frame=entry;vm->queued.branch_window=window;
    vm->pending_flags=0u;vm->pending_valid=1u;
    return 1;
}
static int same_animation(const t3_move_vm_t* vm) {
    t3_motion_stream_t current,next;
    const int a=t3_motion_open(vm->active_bank,vm->common,vm->move.animation_offset,&current);
    const int b=t3_motion_open(vm->pending_bank,vm->common,vm->pending_move.animation_offset,&next);
    if(a<0 || b<0)return -1;
    if(a==1 && b==1)return current.data==next.data &&
        current.key_offset[0]==next.key_offset[0];
    return a==b && vm->active_bank==vm->pending_bank &&
           vm->move.animation_offset==vm->pending_move.animation_offset;
}
int T3_SIM_IMPL(t3_move_vm_begin)(t3_move_vm_t* vm,t3_move_start_facts_t facts) {
    t3_move_vm_t next;
    int ready=0,bind,same;
    if(!vm || !vm->phased || facts.got_hit>1u || facts.alive>1u)return -1;
    next=*vm;
    if(next.pending_valid) {
        ready=t3_move_clock_pending_ready(&next.clock,next.queued.entry_frame,next.move.length);
        if(ready<0)return -1;
    }
    if(ready) {
        t3_move_start_t request=next.queued;
        if(next.transitions==UINT32_MAX)return -1;
        same=same_animation(&next);if(same<0)return -1;
        request.current_length=next.move.length;request.next_length=next.pending_move.length;
        request.root_length=next.root_move.length;
        request.next_active_first=next.pending_move.active_first;
        request.next_active_last=next.pending_move.active_last;
        request.same_animation=(uint8_t)same;request.got_hit=facts.got_hit;
        request.alive=facts.alive;request.hit_freeze_in=facts.hit_freeze_in;
        bind=t3_move_clock_begin(&next.clock,&request);if(bind<0)return -1;
        next.previous_slot=next.slot;
        next.move=next.pending_move;next.active_bank=next.pending_bank;next.slot=next.pending_slot;
        next.current_transition=request.transition;next.current_branch_kind=request.branch_kind;
        next.last_entry_frame=request.branch_kind==3u?1:request.entry_frame;
        next.pending_valid=0u;
        ++next.transitions;
    } else {
        bind=t3_move_clock_advance(&next.clock,next.move.hold_frame);if(bind<0)return -1;
    }
    if(bind) {
        next.root_move=next.move;next.root_bank=next.active_bank;next.root_slot=next.slot;
    }
    *vm=next;return ready;
}
T3_SIM_WRAP(int,t3_move_vm_begin,T3_SIM_MOVE_BEGIN,
    (t3_move_vm_t* vm,t3_move_start_facts_t facts),
    (vm,facts))

int t3_move_vm_queue_input(t3_move_vm_t* vm,const t3_input_history_t* input,
                            const struct t3_branch_context* facts,
                            const t3_command_sequence_t* sequences,size_t sequence_count) {
    t3_branch_record_t branch;
    t3_move_record_t target;
    const t3_divmot_t* owner;
    int16_t entry;
    int16_t window;
    unsigned kind,transition;
    uint8_t flags;
    if(!vm || !vm->phased || !input)return -1;
    if(vm->pending_valid)return 0;
    if(!t3_divmot_branch_find_ex(vm->active_bank,vm->move,input,
            (uint16_t)vm->clock.pose_frame,vm->bank->bank_type,facts,
            sequences,sequence_count,&branch))return 0;
    if(branch.command==0xC000u) {
        const int frame=vm->clock.pose_frame+vm->clock.frame_step;
        const int pose=vm->clock.pose_frame;
        const t3_command_frame_t latest=t3_history_at(input,0);
        const int look=(pose>=(int)branch.first_frame-5 && pose<branch.first_frame &&
                         vm->clock.frame_step>=1 && latest.pressed_buttons) ||
                       (pose>=branch.first_frame && pose<=branch.last_frame &&
                         vm->clock.frame_step>0);
        if(look) {
            t3_branch_record_t found;
            if(!t3_move_vm_resolve(vm->bank,vm->common,branch.target_slot,&target,&owner))return -1;
            if(t3_divmot_branch_find_ex(owner,target,input,1u,vm->bank->bank_type,facts,
                    sequences,sequence_count,&found) && found.command!=0xC000u) {
                if(!t3_move_vm_resolve(vm->bank,vm->common,found.target_slot,&target,&owner))return -1;
                transition=remap(found.flags&63u,&target,vm->last_entry_frame);
                if(!t3_move_vm_queue(vm,found.target_slot,transition,3u,branch.first_frame,
                        (int16_t)(found.last_frame-pose)))return -1;
                vm->pending_flags=(uint8_t)((found.flags&~64u)|(branch.flags&64u));
                return 1;
            }
        }
        if(frame>=1 && frame<=vm->move.length)return 0;
        kind=4u;entry=vm->clock.pose_frame;window=-1;
    } else {
        kind=2u;entry=vm->clock.frame_step<0?branch.last_frame:branch.target_frame;
        if(entry==255)entry=vm->move.length;
        window=(int16_t)(branch.last_frame-vm->clock.pose_frame);
    }
    if(!t3_move_vm_resolve(vm->bank,vm->common,branch.target_slot,&target,&owner))return -1;
    transition=remap(branch.flags&63u,&target,vm->last_entry_frame);
    flags=branch.flags;
    if(!t3_move_vm_queue(vm,branch.target_slot,transition,kind,entry,window))return -1;
    vm->pending_flags=flags;
    return 1;
}
