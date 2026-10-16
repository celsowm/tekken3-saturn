#ifndef T3_PSX_ROUND_H
#define T3_PSX_ROUND_H
#include <stdint.h>
/* Bounded slice policy: begin in FIGHT, hold a result, then reset the arena.
 * Replay, intro and win-pose sequencing are deliberately omitted. */
typedef struct {
    uint16_t timer,hold,result_flags;
    uint8_t number,wins[2],done,match_done;
    int8_t winner;
} t3_psx_round_t;
void t3_psx_round_init(t3_psx_round_t* round);
/* Returns 1 when the next round must restore all fighter state. */
int t3_psx_round_step(t3_psx_round_t* round,const int32_t health[2],int32_t maximum);
#endif
