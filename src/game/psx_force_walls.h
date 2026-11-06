/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of the Tekken Force walls of ArenaBounds (0x80043394 in mode 8, with force.ovl's
 * FUN_800B2F98, FUN_800B3020 and FUN_800B309C) as ported in the pinned Tekken3Remake (tools/research/
 * fight_sim.py _force_walls, docs/research/code/modes.md "Walls"). */
#ifndef T3_PSX_FORCE_WALLS_H
#define T3_PSX_FORCE_WALLS_H
#include <stdint.h>

/* x = z * k / 1000 + c, as the original's FUN_800B3020 / FUN_800B309C. */
typedef struct {int32_t k,c;} t3_force_line_t;
typedef struct {
    t3_force_line_t left,right; /* the player's x limits relative to the camera, by z */
    t3_force_line_t lower_z;    /* on level 4 (stage 18): the lower z limit by the fighter's x relative to the camera */
} t3_force_walls_t;

/* The lines FUN_800B2F98 sets for level 0..3. */
t3_force_walls_t t3_force_walls_for_level(unsigned level);

/* The correction (dx, dz) that brings a fighter back inside: `enemy` is its +0xC5 flag (a CPU enemy
 * stays within 0x1068 of the camera), `view_x` the camera's view-space x (0x800B70F0), `stage` the stage id. */
void t3_force_walls(const t3_force_walls_t* w,int32_t root_x,int32_t root_z,int enemy,int32_t view_x,
                    unsigned stage,int32_t* dx,int32_t* dz);
#endif
