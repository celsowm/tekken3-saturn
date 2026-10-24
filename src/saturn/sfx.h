#ifndef T3_SATURN_SFX_H
#define T3_SATURN_SFX_H

#include <stdint.h>
#include <saturn/core.h>

#if defined(T3_SFX)
/* Combat effects from the original's VAB 0 (scripts/extract_psx_sfx.py): the
 * 8-bit samples are read from SFX.BIN into `scratch` (at least T3_SFX_FILE_BYTES,
 * free only during boot) and handed to the SCSP. */
sat_result_t t3_sfx_init(void* scratch, uint32_t scratch_bytes);
/* One call per displayed frame with t3_combat_events_poll's result: the guard
 * or hit sound for each fighter that was blocked or hit. */
void t3_sfx_play_events(uint8_t events);
/* Also once per displayed image: the round announcer from the round flow. */
void t3_sfx_round_update(void);
uint32_t t3_sfx_played(void);
#else
static inline sat_result_t t3_sfx_init(void* scratch, uint32_t scratch_bytes) {
    (void)scratch;(void)scratch_bytes;return SAT_OK;
}
static inline void t3_sfx_play_events(uint8_t events) { (void)events; }
static inline void t3_sfx_round_update(void) {}
static inline uint32_t t3_sfx_played(void) { return 0; }
#endif

#endif
