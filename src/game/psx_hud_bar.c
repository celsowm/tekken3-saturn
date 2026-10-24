/* Adapted from Tekken3Remake docs/research/code/hud.md and
 * tools/research/draw_sim.py (FUN_8004E55C), PolyForm Noncommercial 1.0.0.
 * Required Notice: Copyright (c) 2026 n0madic. See NOTICE.md. */
#include "psx_hud_bar.h"

enum { BAR_LENGTH=152, REFILL_PER_TICK=3 };

void t3_psx_hud_bar_reset(t3_psx_hud_bar_t* bar) {
    if(bar)*bar=(t3_psx_hud_bar_t){0};
}

int t3_psx_hud_bar_step(t3_psx_hud_bar_t* bar,int32_t health,int32_t max_health) {
    if(!bar || max_health<BAR_LENGTH || health<0 || health>max_health)return 0;
    int target=bar->target;
    if(bar->last_health!=health) {
        bar->last_health=health;
        const int unit=max_health/BAR_LENGTH;
        target=(health-1+unit)/unit;
    }
    if(target>BAR_LENGTH)target=BAR_LENGTH;
    const int drawn=bar->drawn;
    int recent=drawn;
    if(drawn<bar->recent)
        recent=bar->recent-(bar->recent-drawn+15)/16;
    const int grown=target<=drawn || target<drawn+REFILL_PER_TICK?
        target:drawn+REFILL_PER_TICK;
    if(recent<grown)recent=grown;
    bar->target=(int16_t)target;
    bar->drawn=(int16_t)grown;
    bar->recent=(int16_t)recent;
    return 1;
}
