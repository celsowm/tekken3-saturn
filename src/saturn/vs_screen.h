#ifndef T3_SATURN_VS_SCREEN_H
#define T3_SATURN_VS_SCREEN_H
/* The pre-fight VS screen, up for the whole fight load. Rules from Tekken3Remake
 * docs/research/code/modes.md (game state 11). PolyForm Noncommercial 1.0.0,
 * NOTICE.md. Required Notice: Copyright (c) 2026 n0madic. */
#include <stdint.h>
#include "saturn/core.h"

typedef enum {
    T3_VS_ARCADE,           /* arcade and time attack: STAGE n */
    T3_VS_VERSUS,           /* VS: FIGHT n */
    T3_VS_DEMO,             /* the demonstration: STAGE 1 */
    T3_VS_SURVIVAL,         /* survival: FIGHT n */
    T3_VS_PRACTICE          /* PRACTICE MODE */
} t3_vs_kind_t;

/* Reads and shows the screen, using `scratch` until it returns. `number` is the
 * caption's number. Call before anything else writes the VDP2. */
sat_result_t t3_vs_screen_show(void* scratch,uint32_t capacity,const uint8_t picks[2],
                               t3_vs_kind_t kind,unsigned number);
/* Holds until the original's minimum time has passed, fades out and restores the
 * sat_init display mode, leaving the display black. Call before the stage configures the VDP2. */
sat_result_t t3_vs_screen_finish(void);
/* Ends the black. The stage layers have no rotation tables until the first frame
 * is presented, and show one flat colour until then. */
sat_result_t t3_vs_screen_release(void);

#endif
