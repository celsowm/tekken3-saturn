/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * The pictures of practice's key display (practice.ovl FUN_800B7354 and FUN_800B721C; practice_sim.py of the pinned
 * Tekken3Remake), from PKEYS.BIN (scripts/build_practice_icons.py); see NOTICE.md. */
#ifndef T3_PRACTICE_ICONS_H
#define T3_PRACTICE_ICONS_H
#include "saturn/core.h"
#include <stdint.h>

/* Reads PKEYS.BIN into `scratch` and uploads its 68 pictures. Practice only: it takes VDP1 VRAM, and a build or disc
 * without the file (or without room) leaves the key display as text. */
sat_result_t t3_practice_icons_init(void* scratch,unsigned bytes);
int t3_practice_icons_ready(void);
/* Original 384 x 480 coordinates (the key display's y is 400). `pad` is an item's word from t3_practice_keys_items. */
sat_result_t t3_practice_icons_arrow(unsigned pad,unsigned colour,int x,int y);
sat_result_t t3_practice_icons_button(unsigned pad,unsigned colour,int x,int y);
/* A hit marker (kind 0 high, 1 mid, 2 low, 3 unblockable) centred on the original screen point (x, y). */
sat_result_t t3_practice_icons_marker(unsigned kind,int x,int y);
/* The tech-roll line's arrow (up, or down) in menu colour 10, 11 or 7 at the original point (x, y). */
sat_result_t t3_practice_icons_tech_arrow(unsigned down,unsigned colour,int x,int y);
#endif
