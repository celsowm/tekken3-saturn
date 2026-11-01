#ifndef T3_SATURN_VS_SCREEN_H
#define T3_SATURN_VS_SCREEN_H
/* The pre-fight VS screen, up from the end of the select to the end of the fight load.
 * Rules from Tekken3Remake docs/research/code/modes.md (game state 11). PolyForm
 * Noncommercial 1.0.0, NOTICE.md. Required Notice: Copyright (c) 2026 n0madic. */
#include <stdint.h>
#include "saturn/core.h"

/* Bank B: the stage picture streams into bank A while this one is up. */
enum { T3_VS_BITMAP_BASE=0x40000, T3_VS_PALETTE_BANK=7 };

typedef enum {
    T3_VS_ARCADE,           /* arcade and time attack: STAGE n */
    T3_VS_VERSUS,           /* VS: FIGHT n */
    T3_VS_DEMO,             /* the demonstration: STAGE 1 */
    T3_VS_SURVIVAL,         /* survival: FIGHT n */
    T3_VS_PRACTICE          /* PRACTICE MODE */
} t3_vs_kind_t;

/* MENU.BIN. Reads and composes the screen in `scratch`, which must last until
 * t3_vs_screen_present; `number` is the caption's number. Touches nothing on screen. */
sat_result_t t3_vs_screen_compose(void* scratch,uint32_t capacity,const uint8_t picks[2],
                                  t3_vs_kind_t kind,unsigned number);
/* MENU.BIN. Writes the composed screen to VRAM and CRAM and shows it. */
sat_result_t t3_vs_screen_present(void);

/* Shows what t3_vs_screen_present left in VRAM and CRAM. The fight calls this straight
 * after sat_init, which turned the display off. */
sat_result_t t3_vs_screen_resume(void);
/* Holds until the original's minimum time has passed, fades out and restores the
 * sat_init display mode, leaving the display black. Call before the stage configures the VDP2. */
sat_result_t t3_vs_screen_finish(void);
/* Ends the black. The stage layers have no rotation tables until the first frame
 * is presented, and show one flat colour until then. */
sat_result_t t3_vs_screen_release(void);

#endif
