/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Scene OT traversal follows the pinned Tekken3Remake fighter/effects
 * research. See NOTICE.md. */
#ifndef T3_PSX_SCENE_OT_H
#define T3_PSX_SCENE_OT_H
#include <stdint.h>
typedef struct { uint16_t bucket; uint8_t visible,kind; } t3_psx_scene_ot_entry_t;
/* Entries are in actual insertion order, regardless of kind. Visible entries
 * require bucket 0..1023; hidden entries are ignored. `order` and `next`
 * each hold count uint16_t values, are disjoint, and do not overlap entries.
 * Returns visible count or -1 on invalid input; invalid input leaves both
 * output arrays unchanged. Index 65535 is reserved as a chain terminator. */
int t3_psx_scene_ot_order(const t3_psx_scene_ot_entry_t* entries,unsigned count,
                           uint16_t* order,uint16_t* next);
#endif
