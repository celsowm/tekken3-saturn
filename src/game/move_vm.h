#ifndef T3_MOVE_VM_H
#define T3_MOVE_VM_H
#include "divmot.h"

/* Deterministic, no-allocation skeleton of the original move lifecycle:
 * a numbered stance/move slot, frame in move, and branches from the DIVMOT.
 * This is the opcode/transition foundation, NOT full fighter physics.
 *
 * Limitations: only zero-condition/zero-restriction branch rows and the
 * input opcodes in input_history.c execute so far. Unsupported conditions
 * do not match. Frame-event, motion, hit and throw commands are pending.
 */
typedef struct {
    const t3_divmot_t* bank;
    const t3_divmot_t* common;
    const t3_divmot_t* active_bank; /* may be shared divmot99 */
    t3_move_record_t move;
    uint16_t slot;
    uint16_t frame;
    uint32_t transitions;
} t3_move_vm_t;

/* start_slot normally 3 for standing stance, but caller selects by character. */
int t3_move_vm_start(t3_move_vm_t* vm, const t3_divmot_t* bank,
                     const t3_divmot_t* common, unsigned start_slot);

/* -1 bad VM, 0 continued current move, 1 branch changed the move. */
int t3_move_vm_tick(t3_move_vm_t* vm, const t3_input_history_t* input);
int t3_move_vm_tick_ex(t3_move_vm_t* vm, const t3_input_history_t* input,
                       const struct t3_branch_context* facts,
                       const t3_command_sequence_t* sequences,
                       size_t sequence_count);
#endif
