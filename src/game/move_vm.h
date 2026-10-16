#ifndef T3_MOVE_VM_H
#define T3_MOVE_VM_H
#include "divmot.h"
#include "move_clock.h"

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
    t3_move_clock_t clock; /* sole owner of pose/root/move counters */
    uint32_t transitions;
    /* Experimental phased lifecycle. Pose and root may reference DIFFERENT
     * moves during transition 4/5; pending state is not a running move. */
    t3_move_record_t root_move,pending_move;
    const t3_divmot_t *root_bank,*pending_bank;
    t3_move_start_t queued;
    uint16_t root_slot,pending_slot,previous_slot;
    int16_t last_entry_frame;
    uint8_t phased,pending_valid,pending_flags;
    uint8_t current_transition,current_branch_kind;
} t3_move_vm_t;

/* Shared load semantics for live VM and post-hit projection. Applies the
 * authored stream length fixup; both outputs are required and stay unchanged
 * if the move or its present motion stream cannot be decoded. */
int t3_move_vm_resolve(const t3_divmot_t* bank,const t3_divmot_t* common,
                        unsigned slot,t3_move_record_t* record,
                        const t3_divmot_t** owner);

/* start_slot normally 3 for standing stance, but caller selects by character. */
int t3_move_vm_start(t3_move_vm_t* vm, const t3_divmot_t* bank,
                     const t3_divmot_t* common, unsigned start_slot);

/* -1 bad VM, 0 continued current move, 1 branch changed the move. */
int t3_move_vm_tick(t3_move_vm_t* vm, const t3_input_history_t* input);
int t3_move_vm_tick_ex(t3_move_vm_t* vm, const t3_input_history_t* input,
                       const struct t3_branch_context* facts,
                       const t3_command_sequence_t* sequences,
                       size_t sequence_count);

typedef struct {
    uint8_t got_hit,alive;
    int16_t hit_freeze_in;
} t3_move_start_facts_t;
/* Explicit experimental two-phase lifecycle. start uses original one-based
 * pose/root time; decoding the stream uses pose_frame-1. begin runs before
 * physics/contact/events; queue_input runs after this tick's input source.
 * The legacy tick API must not be used on a phased VM. */
int t3_move_vm_start_phased(t3_move_vm_t* vm,const t3_divmot_t* bank,
                            const t3_divmot_t* common,unsigned slot);
int t3_move_vm_queue(t3_move_vm_t* vm,unsigned slot,unsigned transition,
                      unsigned branch_kind,int16_t entry_frame,int16_t branch_window);
int t3_move_vm_begin(t3_move_vm_t* vm,t3_move_start_facts_t facts);
int t3_move_vm_queue_input(t3_move_vm_t* vm,const t3_input_history_t* input,
                            const struct t3_branch_context* facts,
                            const t3_command_sequence_t* sequences,size_t sequence_count);
#endif
