#ifndef T3_BALL_PROGRESS_H
#define T3_BALL_PROGRESS_H
#include <stdint.h>
#include "ball_flow.h"

/* Tekken Ball's opponent state between games: the first-game flag (the original's 0x800982FE, part of its saved progress)
 * and the met counts (ctx + 0x44, which last for one Tekken Ball session). The Saturn record is independent of the PS1
 * save layout. Unpack fills only `met` and `first_done`. */
#define T3_BALL_PROGRESS_BYTES 56u
int t3_ball_progress_pack(const t3_ball_choice_t* in,uint8_t out[T3_BALL_PROGRESS_BYTES]);
int t3_ball_progress_unpack(const uint8_t* in,uint32_t bytes,t3_ball_choice_t* out);
#endif
