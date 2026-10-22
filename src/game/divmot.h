#ifndef T3_DIVMOT_H
#define T3_DIVMOT_H
#include <stdint.h>
#include <stddef.h>
#include "input_history.h"

enum {
    T3_DIVMOT_SECTIONS = 15,
    T3_DIVMOT_MOVE_BYTES = 56,
    T3_DIVMOT_BRANCH_BYTES = 12,
    T3_DIVMOT_MOVE_SLOTS = 4023,
    T3_DIVMOT_EMPTY_SLOT = 0x3FFFFFFF
};
/* Validated zero-copy view of one unrelocated PS1 bank (little-endian).
 * No pointer relocation, malloc or ROM/code emulation. */
struct t3_branch_input_filter;
typedef struct {
    const uint8_t* data;
    size_t size;
    uint32_t sections[T3_DIVMOT_SECTIONS];
    uint16_t move_count;
    uint16_t branch_count;
    uint8_t bank_type;
    const uint8_t* common_branches; /* optional private EXE 12-byte rows */
    uint16_t common_branch_count;
    const struct t3_branch_input_filter *input_filter,*common_input_filter;
} t3_divmot_t;

typedef struct {
    uint32_t state_word;
    uint32_t flags;
    uint32_t animation_offset;
    uint32_t event_index; /* row +0x20: 4-byte records in section 4, -1 absent */
    uint32_t attack_descriptor; /* +0x28: built-in id or section9-relative offset+4 */
    uint32_t branch_index;
    uint16_t attack_word;
    uint16_t damage_class;
    uint16_t stance_slot;
    int16_t step,end_turn;
    uint8_t length, air_start, air_end, hold_frame;
    uint8_t hit_freeze, active_first, active_last;
    uint16_t body_profile,reaction, close_reaction;
} t3_move_record_t;

typedef struct {
    uint16_t command;
    uint8_t restriction, condition;
    uint16_t parameter, target_slot;
    uint8_t flags, first_frame, last_frame, target_frame;
} t3_branch_record_t;

int t3_divmot_open(t3_divmot_t* out, const uint8_t* data, size_t length);
/* Attach a bounded EXE branch table (distinct from the divmot99 move bank).
 * The caller owns its lifetime. Empty input detaches it. */
int t3_divmot_set_common_branches(t3_divmot_t* bank, const uint8_t* data,
                                 size_t length);
/* Attach only matching immutable row indexes. Reopening/detaching common
 * data clears the corresponding attachment. Rebuild after modifying bytes
 * or sequence entries; descriptors and masks must remain immutable/alive. */
int t3_divmot_set_input_filters(t3_divmot_t* bank,
    const struct t3_branch_input_filter* own,const struct t3_branch_input_filter* common);
int t3_divmot_move(const t3_divmot_t* bank, unsigned index,
                   t3_move_record_t* out);
int t3_divmot_branch(const t3_divmot_t* bank, unsigned index,
                     t3_branch_record_t* out);
/* Slot lookup: 1 found, 0 absent, -1 invalid input. 0x40000000 namespace
 * resolves to common bank when supplied (bank_type 99). */
int t3_divmot_slot(const t3_divmot_t* bank, const t3_divmot_t* common,
                   unsigned slot, t3_move_record_t* out);
/* Also return the containing bank: common-bank moves use common branch rows. */
int t3_divmot_slot_resolve(const t3_divmot_t* bank,
                            const t3_divmot_t* common,
                            unsigned slot, t3_move_record_t* out,
                            const t3_divmot_t** owner);
/* First matching supported branch, including EXE common CALL/RETURN when
 * its bounded table is attached. Unknown facts/commands never match;
 * nested calls and invalid table references fail closed. Default continuation
 * at list terminator is returned after current move's final frame. */
struct t3_branch_context;
int t3_divmot_branch_pick_ex(const t3_divmot_t* bank, t3_move_record_t current,
                             const t3_input_history_t* input, unsigned frame,
                             uint8_t own_bank, const struct t3_branch_context* facts,
                             const t3_command_sequence_t* sequences,
                             size_t sequence_count,
                             t3_branch_record_t* picked);
/* Same bounded walker, but returns the default END row regardless of current
 * frame. The original phased scheduler owns forward/backward completion and
 * look-ahead decisions; callers must not execute END as an ordinary command. */
int t3_divmot_branch_find_ex(const t3_divmot_t* bank,t3_move_record_t current,
                             const t3_input_history_t* input,unsigned frame,
                             uint8_t own_bank,const struct t3_branch_context* facts,
                             const t3_command_sequence_t* sequences,size_t sequence_count,
                             t3_branch_record_t* picked);
int t3_divmot_branch_pick(const t3_divmot_t* bank, t3_move_record_t current,
                          const t3_input_history_t* input, unsigned frame,
                          t3_branch_record_t* picked);
#endif
