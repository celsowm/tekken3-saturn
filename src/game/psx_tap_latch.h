#ifndef T3_PSX_TAP_LATCH_H
#define T3_PSX_TAP_LATCH_H
#include <stdint.h>

typedef struct {
    uint16_t previous;
    uint8_t lp,rp,both,condition_used;
} t3_psx_tap_latch_t;

/* LatchButtonTaps (JP Rev.1 0x80040D68). entry is the newest input-history
 * byte: pressed buttons in bits 4..7 and direction in bits 0..3.
 * scratch models the shared $a2, including the original stale-register bug
 * when a consumed tap is frozen. Equal frame counters leave everything alone. */
void t3_psx_tap_latch_step(t3_psx_tap_latch_t* state,uint8_t entry,
                          uint32_t frame,uint32_t seen,uint32_t* scratch);
#endif
