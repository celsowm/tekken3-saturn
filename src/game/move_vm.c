#include "move_vm.h"
#include "psx_motion.h"

/* DivmotLinkBank replaces the raw move-row frame count with the authored
 * stream's first byte. Retail rows can store zero before that load step. */
static int resolve(const t3_divmot_t* bank, const t3_divmot_t* common,
                   unsigned slot, t3_move_record_t* record,
                   const t3_divmot_t** owner,int knots) {
    t3_motion_stream_t motion;
    t3_move_record_t decoded;
    const t3_divmot_t* active=NULL;
    int opened;
    if(!record || !owner ||
       t3_divmot_slot_resolve(bank,common,slot,&decoded,&active)!=1)return 0;
    opened=knots?t3_motion_open(active,common,decoded.animation_offset,&motion):
                 t3_motion_open_header(active,common,decoded.animation_offset,&motion);
    if (opened<0) return 0;
    if (opened==1) decoded.length=motion.frames;
    *record=decoded;*owner=active;
    return 1;
}
int t3_move_vm_resolve(const t3_divmot_t* bank, const t3_divmot_t* common,
                         unsigned slot, t3_move_record_t* record,
                         const t3_divmot_t** owner) {
    return resolve(bank,common,slot,record,owner,1);
}
int t3_move_vm_peek(const t3_divmot_t* bank, const t3_divmot_t* common,
                    unsigned slot, t3_move_record_t* record,
                    const t3_divmot_t** owner) {
    return resolve(bank,common,slot,record,owner,0);
}

int t3_move_vm_start(t3_move_vm_t* vm, const t3_divmot_t* bank,
                     const t3_divmot_t* common, unsigned start_slot) {
    t3_move_record_t record;
    const t3_divmot_t* active = NULL;
    if (!vm || !bank || !bank->data ||
        start_slot >= T3_DIVMOT_MOVE_SLOTS ||
        !t3_move_vm_resolve(bank,common,start_slot,&record,&active)) return 0;
    *vm=(t3_move_vm_t){0};
    vm->bank=bank;
    vm->common=common;
    vm->active_bank=active;
    vm->move=record;
    vm->slot=(uint16_t)start_slot;
    t3_move_clock_reset(&vm->clock,0);
    vm->transitions=0;
    vm->root_bank=active;vm->root_move=record;vm->root_slot=(uint16_t)start_slot;
    return 1;
}

int t3_move_vm_tick_ex(t3_move_vm_t* vm, const t3_input_history_t* input,
                       const struct t3_branch_context* facts,
                       const t3_command_sequence_t* sequences,
                       size_t sequence_count) {
    t3_branch_record_t branch;
    t3_move_record_t next;
    const t3_divmot_t* active = NULL;
    if (!vm || !vm->bank || !input || vm->phased) return -1;
    if (t3_divmot_branch_pick_ex(vm->active_bank,vm->move,input,
                                  (uint16_t)vm->clock.pose_frame,
                                  vm->bank->bank_type,facts,sequences,sequence_count,&branch) &&
        t3_move_vm_resolve(vm->bank,vm->common,branch.target_slot,&next,&active)) {
        vm->move=next;
        vm->active_bank=active;
        vm->slot=branch.target_slot;
        t3_move_clock_reset(&vm->clock,
            branch.target_frame==255u?next.length:branch.target_frame);
        ++vm->transitions;
        vm->root_bank=active;vm->root_move=next;vm->root_slot=vm->slot;
        return 1;
    }
    if(t3_move_clock_advance(&vm->clock,vm->move.hold_frame)<0)return -1;
    return 0;
}

int t3_move_vm_tick(t3_move_vm_t* vm, const t3_input_history_t* input) {
    return t3_move_vm_tick_ex(vm,input,NULL,NULL,0u);
}
