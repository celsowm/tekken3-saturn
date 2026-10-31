#include "run_rules.h"
#include <stdio.h>

static int g_failed;
#define CHECK(c) do{if(!(c)){printf("failed line %d: %s\n",__LINE__,#c);g_failed=1;}}while(0)

int main(void) {
    /* Survival: the level climbs with the wins and every tenth win is level 0. */
    CHECK(t3_survival_level(0)==2 && t3_survival_level(4)==2);
    CHECK(t3_survival_level(5)==3 && t3_survival_level(9)==3);
    CHECK(t3_survival_level(10)==0 && t3_survival_level(11)==5 && t3_survival_level(20)==0);
    CHECK(t3_survival_level(21)==6);
    /* Time attack: levels 4..7. */
    CHECK(t3_time_attack_level(0)==4 && t3_time_attack_level(3)==5);
    CHECK(t3_time_attack_level(6)==6 && t3_time_attack_level(9)==7);
    /* Survival ends at the first loss and goes on after a win. */
    t3_run_step_t s=t3_run_after_match(T3_RUN_SURVIVAL,1,6);
    CHECK(s.wins==7 && !s.finished);
    s=t3_run_after_match(T3_RUN_SURVIVAL,0,6);
    CHECK(s.wins==6 && s.finished);
    /* Time attack: a loss repeats the stage, the tenth win ends the run. */
    s=t3_run_after_match(T3_RUN_TIME_ATTACK,0,4);
    CHECK(s.wins==4 && !s.finished);
    s=t3_run_after_match(T3_RUN_TIME_ATTACK,1,8);
    CHECK(s.wins==9 && !s.finished);
    s=t3_run_after_match(T3_RUN_TIME_ATTACK,1,9);
    CHECK(s.wins==10 && s.finished);
    /* Arcade runs never end here. */
    s=t3_run_after_match(T3_RUN_ARCADE,1,3);
    CHECK(s.wins==4 && !s.finished);
    if(!g_failed)printf("run rules ok\n");
    return g_failed;
}
