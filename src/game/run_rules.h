#ifndef T3_GAME_RUN_RULES_H
#define T3_GAME_RUN_RULES_H
/* Survival and time attack runs. Rules from Tekken3Remake docs/research/code/modes.md
 * (survival CPU level `0x800B104C`; time attack: the arcade ladder, ten fights).
 * PolyForm Noncommercial 1.0.0, NOTICE.md. Required Notice: Copyright (c) 2026 n0madic. */
#include <stdint.h>

enum { T3_RUN_ARCADE=0, T3_RUN_SURVIVAL=1, T3_RUN_TIME_ATTACK=2, T3_TIME_ATTACK_FIGHTS=10 };

typedef struct {
    uint8_t wins;
    uint8_t finished;       /* the run is over: show the result, otherwise pick the next opponent */
} t3_run_step_t;

/* Survival's CPU level from the wins: the middle of each range, level 0 on every tenth win. */
uint8_t t3_survival_level(unsigned wins);
/* Time attack: the arcade ladder's levels 4-7, rising every three wins. */
uint8_t t3_time_attack_level(unsigned wins);
/* What follows a finished match of a run (`won`: player 1 won). */
t3_run_step_t t3_run_after_match(unsigned mode,int won,unsigned wins);

#endif
