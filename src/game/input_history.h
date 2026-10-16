#ifndef T3_INPUT_HISTORY_H
#define T3_INPUT_HISTORY_H

#include <stdint.h>
#include <stddef.h>
#include "fight.h"

/* Neutral numpad=5, toward rival=6, away=4; high/low=8/2.
 * Button bits 0..3 match original PS1 command words. */
void t3_history_reset(t3_input_history_t* h);
void t3_history_push(t3_input_history_t* h, t3_input_t input);
t3_command_frame_t t3_history_at(const t3_input_history_t* h, unsigned age);

/* Original move-bank input command format:
 * <0xC000: buttons, any-button and numpad direction mask.
 * 0xC001/C002: 6,5,6 / 4,5,4 double tap within 20 frames;
 * 0xC00C/0xC00D: CALL/RETURN belong to the branch walker, not input tests.
 * Other special commands require bank sequence data.
 * Returns 0 for unsupported rather than pretending a move is valid. */
int t3_command_matches(uint16_t command, const t3_input_history_t* h);

/* User-supplied PS1 command tables: 63 + 41 EXE-defined sequence IDs.
 * Missing entries return false, never a guessed command. */
typedef struct {
    uint16_t command, window;
    uint16_t steps[T3_INPUT_SEQUENCE_MAX+1u]; /* zero terminated */
} t3_command_sequence_t;
int t3_command_matches_table(uint16_t command, const t3_input_history_t* h,
                             const t3_command_sequence_t* table, size_t count);

/* Sequence words are (buttons << 8) | numpad direction, most recent last.
 * 'steps' is a zero-terminated table of at most T3_INPUT_SEQUENCE_MAX entries.
 * Retains original history (doesn't destructively consume commands). */
int t3_sequence_matches(const t3_input_history_t* h,
                        const uint16_t* steps, unsigned max_window);

#endif
