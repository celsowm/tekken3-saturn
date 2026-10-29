/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * KMD projected vertex blocks follow Tekken3Remake's 3dmk-models research.
 * See NOTICE.md. */
#include "psx_projected_mesh.h"

int t3_psx_projected_mesh_step(t3_psx_vertex_exchange_t* state,
    const t3_psx_projected_part_t* part, const t3_psx_camera_matrix_t* matrix,
    t3_psx_projected_vertex_t* vertices, size_t vertex_count) {
    if (!state || !part || !matrix || (part->map_count && (!vertices || !part->logical_map))) return 0;
    if (part->scratch_count > T3_PSX_VERTEX_SLOTS || part->global_count > T3_PSX_VERTEX_SLOTS ||
        part->own_count > T3_PSX_VERTEX_SLOTS ||
        part->scratch_count + part->global_count + part->own_count > T3_PSX_VERTEX_SLOTS) return 0;
    const size_t slots = part->scratch_count + part->global_count + part->own_count;
    for (size_t i = 0; i < part->map_count; ++i)
        if (part->logical_map[2*i] >= slots || part->logical_map[2*i+1] >= vertex_count) return 0;
    t3_psx_vertex_block_t block = {0};
    block.scratch_import = part->scratch_import; block.scratch_count = part->scratch_count;
    block.global_import = part->global_import; block.global_count = part->global_count;
    block.own_count = part->own_count;
    for (unsigned i = 0; i < 6; ++i) { block.post[i] = part->post[i]; block.post_count[i] = part->post_count[i]; }
    if (!t3_psx_vertex_project_exchange(state, &block, matrix, part->positions)) return 0;
    for (size_t i = 0; i < part->map_count; ++i) {
        const unsigned slot = part->logical_map[2*i], index = part->logical_map[2*i+1];
        vertices[index].xy = state->scratch.xy[slot];
        vertices[index].z = state->scratch.z[slot];
    }
    return 1;
}

int t3_psx_projected_mesh_build(const t3_psx_projected_part_t* parts, size_t part_count,
    const t3_psx_camera_matrix_t* view, const t3_psx_joint_t joints[T3_PSX_DRAW_PARTS],
    uint32_t joint_mask, t3_psx_projected_vertex_t* vertices, size_t vertex_count) {
    if (!parts || !view || !joints || (vertex_count && !vertices)) return 0;
    t3_psx_vertex_exchange_t exchange = {0};
    t3_psx_camera_matrix_t matrix;
    unsigned current = T3_PSX_DRAW_PARTS;
    for (size_t i = 0; i < part_count; ++i) {
        unsigned part = parts[i].part;
        if (part >= T3_PSX_DRAW_PARTS || !(joint_mask & (1u << part)) ||
            (current != T3_PSX_DRAW_PARTS && part < current)) return 0;
        if (part != current) {
            if (!t3_psx_camera_joint_matrix(view, &joints[part], &matrix)) return 0;
            current = part;
        }
        if (!t3_psx_projected_mesh_step(&exchange, &parts[i], &matrix, vertices, vertex_count)) return 0;
    }
    return 1;
}
