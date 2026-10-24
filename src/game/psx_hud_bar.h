#ifndef T3_PSX_HUD_BAR_H
#define T3_PSX_HUD_BAR_H
#include <stdint.h>

/* FUN_8004E55C's four persistent fields, in original 152-pixel units. */
typedef struct {
    int32_t last_health;
    int16_t target;
    int16_t drawn;
    int16_t recent;
} t3_psx_hud_bar_t;

void t3_psx_hud_bar_reset(t3_psx_hud_bar_t* bar);
int t3_psx_hud_bar_step(t3_psx_hud_bar_t* bar,int32_t health,int32_t max_health);
#endif
