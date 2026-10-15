#include "move_vm.h"

int t3_move_vm_start(t3_move_vm_t* vm, const t3_divmot_t* bank,
                     const t3_divmot_t* common, unsigned start_slot) {
    t3_move_record_t record;
    const t3_divmot_t* active = NULL;
    if (!vm || !bank || !bank->data ||
        start_slot >= T3_DIVMOT_MOVE_SLOTS ||
        t3_divmot_slot_resolve(bank,common,start_slot,&record,&active)!=1) return 0;
    vm->bank=bank;
    vm->common=common;
    vm->active_bank=active;
    vm->move=record;
    vm->slot=(uint16_t)start_slot;
    vm->frame=0;
    vm->transitions=0;
    return 1;
}

int t3_move_vm_tick_ex(t3_move_vm_t* vm, const t3_input_history_t* input,
                       const struct t3_branch_context* facts,
                       const t3_command_sequence_t* sequences,
                       size_t sequence_count) {
    t3_branch_record_t branch;
    t3_move_record_t next;
    const t3_divmot_t* active = NULL;
    if (!vm || !vm->bank || !input) return -1;
    if (t3_divmot_branch_pick_ex(vm->active_bank,vm->move,input,vm->frame,
                                  facts,sequences,sequence_count,&branch) &&
        t3_divmot_slot_resolve(vm->bank,vm->common,branch.target_slot,&next,&active)==1) {
        vm->move=next;
        vm->active_bank=active;
        vm->slot=branch.target_slot;
        vm->frame=branch.target_frame==255u?next.length:branch.target_frame;
        ++vm->transitions;
        return 1;
    }
    if (vm->frame < 0xfffeu) ++vm->frame;
    return 0;
}

int t3_move_vm_tick(t3_move_vm_t* vm, const t3_input_history_t* input) {
    return t3_move_vm_tick_ex(vm,input,NULL,NULL,0u);
}
