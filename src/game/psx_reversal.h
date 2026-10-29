#ifndef T3_PSX_REVERSAL_H
#define T3_PSX_REVERSAL_H
#include <stdint.h>
#include "branch_rules.h"

typedef struct {
    int16_t self_angle,opponent_angle,opponent_attack,opponent_slot;
    uint8_t got_hit,joints[4];
    uint32_t opponent_flags;
    /* MoveInSlotList: the original sign-extends the list parameter. The
     * caller owns the original lists and animation-tag lookup. */
    int (*animation_in_list)(void* user,int16_t list);
    void* user;
} t3_psx_reversal_facts_t;

/* ReversalCondition (JP Rev.1 0x8002EB24), separate from ordinary branch
 * conditions. Nonzero on a match. No fighter state is changed here. */
int t3_psx_reversal_condition(uint8_t type,uint16_t parameter,
                             const t3_psx_reversal_facts_t* facts);
/* Packed private positive-tag lists: u16 count, count+1 byte offsets,
 * zero-terminated s16 lists. -1 means unavailable/malformed, not no match. */
int t3_psx_reversal_list_has(const uint8_t* data,size_t size,int16_t list,int16_t tag);
/* Original single-return-address common-row walker. Callback: -1 missing
 * facts, 0 no match, 1 match. Return -1 malformed/missing, 0 END, 1 found. */
int t3_psx_reversal_find(const t3_divmot_t* bank,t3_move_record_t move,
    const t3_input_history_t* input,unsigned frame,uint8_t own_bank,
    const t3_branch_context_t* restrictions,const t3_command_sequence_t* sequences,size_t sequence_count,
    int (*condition)(void*,const t3_branch_record_t*),void* user,t3_branch_record_t* out);
#endif
