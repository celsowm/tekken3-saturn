/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic. See NOTICE.md. */
#ifndef T3_STAGE_RULES_H
#define T3_STAGE_RULES_H
#include <stdint.h>
/* The fighter whose home stage and theme a fight uses: side 0 in the demo, side 1
 * (the CPU in a one-player fight) otherwise. */
unsigned t3_stage_side(int demo);
/* A True Ogre fight has no panorama and a spotlight floor. */
int t3_stage_true_ogre(const uint8_t picks[2]);
#endif
