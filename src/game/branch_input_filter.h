#ifndef T3_BRANCH_INPUT_FILTER_H
#define T3_BRANCH_INPUT_FILTER_H
#include "input_history.h"

enum { T3_BRANCH_INPUT_CLASSES=10, T3_BRANCH_INPUT_UNFILTERED=10 };
/* Derived index over immutable 12-byte rows and an immutable sequence table.
 * Class 0 is InputClear; classes 1..9 are numpad directions, with no newly
 * pressed buttons. A set bit is a possible command, never a selected branch.
 * All storage belongs to the caller and must outlive any attached bank. */
typedef struct t3_branch_input_filter {
    const uint8_t* rows;
    const t3_command_sequence_t* sequences;
    size_t sequence_count;
    uint32_t* words;
    uint16_t row_count,word_count;
} t3_branch_input_filter_t;

int t3_branch_input_filter_init(t3_branch_input_filter_t* out,
    const uint8_t* rows,size_t row_bytes,const t3_command_sequence_t* sequences,
    size_t sequence_count,uint32_t* words,size_t word_capacity);
unsigned t3_branch_input_filter_class(const t3_input_history_t* input);
int t3_branch_input_filter_next(const t3_branch_input_filter_t* filter,
    unsigned input_class,unsigned first,unsigned* next);
#endif
