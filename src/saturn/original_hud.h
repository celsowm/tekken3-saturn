#ifndef T3_ORIGINAL_HUD_H
#define T3_ORIGINAL_HUD_H
#include "saturn/core.h"
#include "psx_viewport.h"

/* Original mode-0 2D rectangle: (0,20,368,448), stored at Rev.1
 * 0x800B1190. Its x origin is independent of the 3D projection centre. */
static inline int t3_original_hud_x(int x) {
    return t3_psx_viewport_x(x);
}

/* Private system-texture sprites in HUD.BIN, generated from the user's disc. */
sat_result_t t3_original_hud_init(void* scratch,unsigned bytes);
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
/* Clear the particle cells under the HUD's rectangles (it is drawn last). */
sat_result_t t3_original_hud_occlude_particles(void);
#endif
sat_result_t t3_original_hud_bar(int x,int y,int width,int length,int recent,int inner_right);
/* The bar's `length`/`recent` arrive already in fill pixels (the original's
 * 152 units mapped by the caller); width is the full capped span. */
sat_result_t t3_original_hud_timer(unsigned seconds,int x,int y);
sat_result_t t3_original_hud_mark(unsigned player,unsigned won,int x,int y);
/* FUN_8004EB9C: the plate quad stretched under a name, and the padded name
 * sheet (width = padded texels scaled by the screen mapping). */
sat_result_t t3_original_hud_plate(int x,int y,int width);
sat_result_t t3_original_hud_name(unsigned player,int x,int y,int width);
/* FUN_8004D15C: original colour/font and 384 x 480 coordinates. Font 2
 * contains the round messages; font 1 contains REPLAY in colour 7. */
sat_result_t t3_original_hud_text(const char* text,unsigned colour,unsigned font,int x,int y);
#if defined(T3_FIGHTER_PACKS)
/* Replaces the two name sheets with the chosen fighters' (pack section). */
sat_result_t t3_original_hud_load_names(void* scratch,unsigned bytes);
#endif
#endif
