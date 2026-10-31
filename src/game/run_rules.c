#include "run_rules.h"

uint8_t t3_survival_level(unsigned wins) {
    if(wins && wins%10u==0)return 0u;
    return wins<5u?2u:wins<10u?3u:wins<20u?5u:6u;
}

uint8_t t3_time_attack_level(unsigned wins) {
    return (uint8_t)(4u+(wins>=9u?3u:wins/3u));
}

t3_run_step_t t3_run_after_match(unsigned mode,int won,unsigned wins) {
    t3_run_step_t step={(uint8_t)(wins+(won?1u:0u)),0};
    if(mode==T3_RUN_SURVIVAL)step.finished=!won;
    else if(mode==T3_RUN_TIME_ATTACK)step.finished=step.wins>=T3_TIME_ATTACK_FIGHTS;
    return step;
}
