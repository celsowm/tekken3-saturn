/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Original KMD projected vertex layout follows Tekken3Remake's 3dmk-models
 * research. Private model data is generated locally. See NOTICE.md. */
#ifndef T3_PSX_PROJECTED_MESH_H
#define T3_PSX_PROJECTED_MESH_H
#include <stdint.h>
#include <stddef.h>
#include "psx_vertex_project.h"
typedef struct {
    unsigned part;
    const uint8_t* scratch_import; size_t scratch_count;
    const uint8_t* global_import; size_t global_count;
    const int16_t (*positions)[3]; size_t own_count;
    const uint16_t* post[6]; size_t post_count[6];
    /* Pairs of projected scratch slot and compiled render vertex index. */
    const uint16_t* map;
    /* Parallel pairs of scratch slot and distinct original logical index. */
    const uint16_t* logical_map; size_t map_count;
} t3_psx_projected_part_t;
typedef struct { uint32_t xy; int16_t z; } t3_psx_projected_vertex_t;
/* One block in draw order; state persists between blocks of one fighter.
 * The logical map keeps distinct PS1 seam slots even when SKA merges them.
 * Invalid inputs leave the exchange state and output unchanged. */
int t3_psx_projected_mesh_step(t3_psx_vertex_exchange_t* state,
    const t3_psx_projected_part_t* part, const t3_psx_camera_matrix_t* matrix,
    t3_psx_projected_vertex_t* vertices, size_t vertex_count);
/* Build a fighter in draw order from the camera and draw-part world joints.
 * Consecutive blocks can share a joint; exchange state is private to this
 * build. On failure the output can contain a partial fighter. */
int t3_psx_projected_mesh_build(const t3_psx_projected_part_t* parts, size_t part_count,
    const t3_psx_camera_matrix_t* view, const t3_psx_joint_t joints[T3_PSX_DRAW_PARTS],
    uint32_t joint_mask, t3_psx_projected_vertex_t* vertices, size_t vertex_count);
#endif
