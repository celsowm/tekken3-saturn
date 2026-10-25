#ifndef T3_ORIGINAL_HUD_H
#define T3_ORIGINAL_HUD_H
#include "saturn/core.h"

/* Private system-texture sprites in HUD.BIN, generated from the user's disc. */
sat_result_t t3_original_hud_init(void);
sat_result_t t3_original_hud_bar(int x,int y,int width,int length,int recent,int inner_right);
/* The bar's `length`/`recent` arrive already in fill pixels (the original's
 * 152 units mapped by the caller); width is the full capped span. */
sat_result_t t3_original_hud_timer(unsigned seconds,int x,int y);
sat_result_t t3_original_hud_mark(unsigned player,unsigned won,int x,int y);
/* FUN_8004EB9C: the plate quad stretched under a name, and the padded name
 * sheet (width = padded texels scaled by the screen mapping). */
sat_result_t t3_original_hud_plate(int x,int y,int width);
sat_result_t t3_original_hud_name(unsigned player,int x,int y,int width);
#endif
