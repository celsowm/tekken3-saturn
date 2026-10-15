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
typedef struct {
    const uint8_t* data;
    size_t size;
    uint32_t sections[T3_DIVMOT_SECTIONS];
    uint16_t move_count;
    uint16_t branch_count;
    uint8_t bank_type;
} t3_divmot_t;

typedef struct {
    uint32_t state_word;
    uint32_t flags;
    uint32_t animation_offset;
    uint32_t branch_index;
    uint16_t attack_word;
    uint16_t damage_class;
    uint16_t stance_slot;
    int16_t step;
    uint8_t length, air_start, air_end, hold_frame;
    uint8_t hit_freeze, active_first, active_last;
    uint16_t reaction, close_reaction;
} t3_move_record_t;

typedef struct {
    uint16_t command;
    uint8_t restriction, condition;
    uint16_t parameter, target_slot;
    uint8_t flags, first_frame, last_frame, target_frame;
} t3_branch_record_t;

int t3_divmot_open(t3_divmot_t* out, const uint8_t* data, size_t length);
int t3_divmot_move(const t3_divmot_t* bank, unsigned index,
                   t3_move_record_t* out);
int t3_divmot_branch(const t3_divmot_t* bank, unsigned index,
                     t3_branch_record_t* out);
/* Slot lookup: 1 found, 0 absent, -1 invalid input. 0x40000000 namespace
 * resolves to common bank when supplied (bank_type 99). */
int t3_divmot_slot(const t3_divmot_t* bank, const t3_divmot_t* common,
                   unsigned slot, t3_move_record_t* out);
/* First matching branch. Only unconditional branch conditions and command
 * words implemented by t3_command_matches are accepted. An unsupported
 * branch condition is not erroneously treated as true. Default continuation
 * at list terminator is returned after current move's final frame. */
int t3_divmot_branch_pick(const t3_divmot_t* bank, t3_move_record_t current,
                          const t3_input_history_t* input, unsigned frame,
                          t3_branch_record_t* picked);
#endif
