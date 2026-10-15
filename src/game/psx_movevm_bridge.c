#include "psx_movevm_bridge.h"
#include <stdint.h>

/* Check the few decoded fields used by the interpreter to catch a stale
 * MoveVM snapshot. Comparing the whole struct would include C padding.
 */
static int source_is_current(const t3_move_vm_t* vm) {
    const t3_divmot_t* owner=NULL;
    t3_move_record_t decoded;
    if(!vm || !vm->bank || !vm->bank->data ||
       vm->bank->bank_type==99u || !vm->active_bank ||
       vm->slot>=T3_DIVMOT_MOVE_SLOTS ||
       t3_divmot_slot_resolve(vm->bank,vm->common,vm->slot,
                              &decoded,&owner)!=1 ||
       owner!=vm->active_bank)
        return 0;
    return decoded.animation_offset==vm->move.animation_offset &&
           decoded.branch_index==vm->move.branch_index &&
           decoded.state_word==vm->move.state_word &&
           decoded.attack_word==vm->move.attack_word &&
           decoded.length==vm->move.length;
}
int t3_psx_movevm_project(const t3_move_vm_t* before,
                          unsigned next_slot, unsigned next_pose_frame,
                          t3_move_vm_t* out) {
    t3_move_record_t target;
    const t3_divmot_t* owner=NULL;
    t3_move_vm_t staged;
    if(!out || !source_is_current(before) ||
       next_slot>=T3_DIVMOT_MOVE_SLOTS ||
       t3_divmot_slot_resolve(before->bank,before->common,
                             next_slot,&target,&owner)!=1 ||
       !owner || next_pose_frame>target.length ||
       (next_slot!=before->slot && before->transitions==UINT32_MAX))
        return 0;
    staged=*before;
    staged.active_bank=owner;
    staged.move=target;
    staged.slot=(uint16_t)next_slot;
    staged.frame=(uint16_t)next_pose_frame;
    if(next_slot!=before->slot)++staged.transitions;
    *out=staged;
    return 1;
}
int t3_psx_movevm_bridge_preview_pair(
    const t3_psx_combat_attack_t attacks[2],
    const t3_psx_hit_apply_frame_t defender_frames[2],
    const t3_psx_fighter_transition_state_t before[2],
    const t3_psx_transition_facts_t transition_facts[2],
    const t3_move_vm_t vms[2],
    const t3_psx_pose_frame_fact_t pose_after[2],
    t3_psx_movevm_bridge_preview_t* out) {
    t3_psx_movevm_bridge_preview_t staged={0};
    unsigned i;
    if(!attacks || !defender_frames || !before || !transition_facts ||
       !vms || !pose_after || !out)
        return 0;
    for(i=0u;i<2u;++i) {
        if(pose_after[i].pose_frame_known!=1u ||
           !source_is_current(&vms[i]) ||
           before[i].move_slot!=vms[i].slot)
            return 0;
    }
    if(!t3_psx_combat_transition_preview_pair(
            attacks,defender_frames,before,transition_facts,
            &staged.transition))
        return 0;
    for(i=0u;i<2u;++i) {
        const t3_psx_fighter_transition_state_t* next=
            &staged.transition.fighter[i];
        if(next->phase==T3_PSX_PHASE_KO ||
           !t3_psx_movevm_project(&vms[i],next->move_slot,
                                  pose_after[i].pose_frame,&staged.vm[i]))
            return 0;
        staged.slot_changed[i]=(uint8_t)(staged.vm[i].slot!=vms[i].slot);
        staged.pose_frame_changed[i]=(uint8_t)(
            staged.vm[i].frame!=vms[i].frame);
    }
    *out=staged;
    return 1;
}
