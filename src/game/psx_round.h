#ifndef T3_PSX_ROUND_H
#define T3_PSX_ROUND_H
#include <stdint.h>
/* Bounded slice policy: begin in FIGHT, hold a result, then reset the arena.
 * Replay, intro and win-pose sequencing are deliberately omitted. */
typedef struct {
    uint16_t timer,hold,result_flags;
    uint8_t number,wins[2],done,match_done;
    int8_t winner;
    uint8_t max_rounds;
} t3_psx_round_t;
/* Original normal VS RoundWins/MatchResult rules; health-left is the original
 * unsigned 1/4096 fraction. Configured tie policy is explicit. */
typedef struct {
    uint16_t wins[2],played,to_win;
    int16_t max_rounds;
    uint8_t human_mask,tie_choice,draw_wins;
    int8_t round_won[2],match_winner,match_loser;
    uint8_t match_result;
} t3_psx_round_score_t;
/* Failure leaves the state unchanged. Normal VS limits only, no team modes. */
int t3_psx_round_award(t3_psx_round_score_t* score,uint16_t first,uint16_t second);
void t3_psx_round_init(t3_psx_round_t* round);
/* Returns 1 when the next round must restore all fighter state. */
int t3_psx_round_step(t3_psx_round_t* round,const int32_t health[2],int32_t maximum);
#endif
