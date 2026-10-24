#ifndef T3_ORIGINAL_HUD_H
#define T3_ORIGINAL_HUD_H
#include "saturn/core.h"

/* Private system-texture sprites in HUD.BIN, generated from the user's disc. */
sat_result_t t3_original_hud_init(void);
sat_result_t t3_original_hud_bar(int x,int y,int width,int length,int recent,int inner_right);
sat_result_t t3_original_hud_timer(unsigned seconds,int x,int y);
sat_result_t t3_original_hud_mark(unsigned player,unsigned won,int x,int y);
#endif
