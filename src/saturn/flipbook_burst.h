#ifndef T3_SATURN_FLIPBOOK_BURST_H
#define T3_SATURN_FLIPBOOK_BURST_H
#include <stdint.h>
#include "saturn/core.h"

#define T3_FLIPBOOK_FILE_BYTES 13872u

#if defined(T3_FLIPBOOK_BURST)
/* The original's hit burst (flipbook set 3) as an additive VDP1 sprite over
 * the VDP2 image. `scratch` (at least T3_FLIPBOOK_FILE_BYTES, free only during
 * boot) holds FLIPBOOK.BIN while its 27 frames are expanded into VDP1 VRAM. */
sat_result_t t3_flipbook_burst_init(void* scratch,uint32_t scratch_bytes);
/* Once per displayed image, after the fighters: one sprite per active record
 * of the latest captured particle image. */
sat_result_t t3_flipbook_burst_draw(void);
uint32_t t3_flipbook_burst_drawn(void);
#else
static inline sat_result_t t3_flipbook_burst_init(void* scratch,uint32_t scratch_bytes) {
    (void)scratch;(void)scratch_bytes;return SAT_OK;
}
static inline sat_result_t t3_flipbook_burst_draw(void) { return SAT_OK; }
static inline uint32_t t3_flipbook_burst_drawn(void) { return 0; }
#endif
#endif
