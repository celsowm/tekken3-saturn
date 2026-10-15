#ifndef T3_INPUT_HISTORY_H
#define T3_INPUT_HISTORY_H

#include <stdint.h>
#include "fight.h"

/* Neutral numpad=5, toward rival=6, away=4; high/low=8/2.
 * Button bits 0..3 match original PS1 command words. */
void t3_history_reset(t3_input_history_t* h);
void t3_history_push(t3_input_history_t* h, t3_input_t input);
t3_command_frame_t t3_history_at(const t3_input_history_t* h, unsigned age);

/* Original move-bank input command format:
 * <0xC000: buttons, any-button and numpad direction mask.
 * 0xC001/C002: 6,5,6 / 4,5,4 double tap within 20 frames;
 * 0xC00D: condition-only. Other special commands require bank sequence data.
 * Returns 0 for unsupported rather than pretending a move is valid. */
int t3_command_matches(uint16_t command, const t3_input_history_t* h);

/* Sequence words are (buttons << 8) | numpad direction, most recent last.
 * 'steps' is a zero-terminated table of at most T3_INPUT_SEQUENCE_MAX entries.
 * Retains original history (doesn't destructively consume commands). */
int t3_sequence_matches(const t3_input_history_t* h,
                        const uint16_t* steps, unsigned max_window);

#endif
