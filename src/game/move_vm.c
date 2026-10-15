#include "move_vm.h"

int t3_move_vm_start(t3_move_vm_t* vm, const t3_divmot_t* bank,
                     const t3_divmot_t* common, unsigned start_slot) {
    t3_move_record_t record;
    if (!vm || !bank || !bank->data ||
        start_slot >= T3_DIVMOT_MOVE_SLOTS ||
        t3_divmot_slot(bank,common,start_slot,&record)!=1) return 0;
    vm->bank=bank;
    vm->common=common;
    vm->move=record;
    vm->slot=(uint16_t)start_slot;
    vm->frame=0;
    vm->transitions=0;
    return 1;
}

int t3_move_vm_tick(t3_move_vm_t* vm, const t3_input_history_t* input) {
    t3_branch_record_t branch;
    t3_move_record_t next;
    if (!vm || !vm->bank || !input) return -1;
    if (t3_divmot_branch_pick(vm->bank,vm->move,input,vm->frame,&branch) &&
        t3_divmot_slot(vm->bank,vm->common,branch.target_slot,&next)==1) {
        vm->move=next;
        vm->slot=branch.target_slot;
        vm->frame=branch.target_frame==255u?next.length:branch.target_frame;
        ++vm->transitions;
        return 1;
    }
    if (vm->frame < 0xfffeu) ++vm->frame;
    return 0;
}
