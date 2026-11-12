/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of the Tekken Force floor's tile layout as documented in the pinned Tekken3Remake
 * (docs/research/code/stages.md "Floor": the 10 x 10 descriptor, the corner sets of FloorSetup and their orders; the
 * remake's stage_view.gd draws the same pattern in world space, 1,800-unit tiles, cut at the backdrop's edge). */
#ifndef T3_PSX_FORCE_FLOOR_H
#define T3_PSX_FORCE_FLOOR_H
#include <stdint.h>

#define T3_FORCE_FLOOR_TILE 1800     /* world units per tile */
#define T3_FORCE_FLOOR_PATTERN 10    /* the descriptor is 10 x 10 entries, repeating */
#define T3_FORCE_FLOOR_EDGE 0x9C4    /* the tile map covers the floor past z = 2,500 (FORCE_HORIZON_Z) */

/* The descriptor entry (0..99) under world tile column `tx` and row `r` (row 0 is the one ending at the edge, rows
 * run towards -z). The pattern is anchored to the world in x and to the backdrop's edge in z. */
unsigned t3_force_floor_entry(int32_t tx,int32_t r);

/* World corners of a tile in the order v0 (x0, z1), v1 (x1, z1), v2 (x0, z0), v3 (x1, z0): x in x, z in z. */
void t3_force_floor_corners(int32_t tx,int32_t r,int32_t x[4],int32_t z[4]);

/* `orient` = corner set * 4 + order (descriptor word 0 bits 30-31 and 25-26). Fills tl, tr, br, bl with the index
 * (0..3, v0..v3) of the vertex whose texture coordinate is the tile's top-left (0,0), top-right (63,0),
 * bottom-right (63,63) and bottom-left (0,63): the order a textured quad takes its corners in. */
void t3_force_floor_quad_order(unsigned orient,uint8_t out[4]);
#endif
