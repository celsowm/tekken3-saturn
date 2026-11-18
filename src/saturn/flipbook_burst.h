#ifndef T3_SATURN_FLIPBOOK_BURST_H
#define T3_SATURN_FLIPBOOK_BURST_H
#include <stdint.h>
#include "saturn/core.h"

#define T3_FLIPBOOK_FILE_BYTES 22528u /* the largest file: FIRE.BIN, 22462 bytes */

#if defined(T3_FLIPBOOK_BURST)
/* The original's hit burst (flipbook set 3) as an additive VDP1 sprite over
 * the VDP2 image. `scratch` (at least T3_FLIPBOOK_FILE_BYTES, free only during
 * boot) holds FLIPBOOK.BIN while its 27 frames are expanded into VDP1 VRAM. */
sat_result_t t3_flipbook_burst_init(void* scratch,uint32_t scratch_bytes);
/* Once per displayed image, after the fighters: one sprite per active record
 * of the latest captured particle image. */
sat_result_t t3_flipbook_burst_draw(void);
sat_result_t t3_flipbook_burst_draw_behind(void);
uint32_t t3_flipbook_burst_drawn(void);
/* Tekken Ball's charged-ball glow (effect type 15): `kind` is the ball's render kind (6 or 7: the hitting player's flipbook,
 * 9: a flame), `tick` counts its frames since the kind began; the sprite is centred at (cx, cy) with these half sizes (screen pixels). */
sat_result_t t3_flipbook_burst_ball_glow(int cx,int cy,int half_w,int half_h,unsigned kind,unsigned tick);
#else
static inline sat_result_t t3_flipbook_burst_init(void* scratch,uint32_t scratch_bytes) {
    (void)scratch;(void)scratch_bytes;return SAT_OK;
}
static inline sat_result_t t3_flipbook_burst_draw(void) { return SAT_OK; }
static inline sat_result_t t3_flipbook_burst_draw_behind(void) { return SAT_OK; }
static inline uint32_t t3_flipbook_burst_drawn(void) { return 0; }
static inline sat_result_t t3_flipbook_burst_ball_glow(int cx,int cy,int half_w,int half_h,unsigned kind,unsigned tick) {
    (void)cx;(void)cy;(void)half_w;(void)half_h;(void)kind;(void)tick;return SAT_OK;
}
#endif
#endif
