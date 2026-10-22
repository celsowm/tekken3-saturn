#ifndef T3_COMPILED_RENDERER_H
#define T3_COMPILED_RENDERER_H

#include "psx_skin_bridge.h"
#include "saturn/skin3d.h"

/* Face and vertex identities are the SKA1 identities. Anchors, indices,
 * materials and two-sided flags remain in their existing immutable arrays. */
typedef struct {
    uint16_t vertex_count, face_count, joint_count, dependency_count;
    uint32_t maximum_l1, joint_mask;
    uint8_t exact_skin_supported;
    const uint16_t *joint_offsets, *rigid_vertices;
    const uint16_t *vertex_offsets, *vertex_faces;
    const uint8_t* face_joints; /* 255: retain ordinary visibility rules */
} t3_compiled_plan_t;

typedef struct {
    const sat_skin_asset_t* skin;
    const t3_compiled_plan_t* plan;
} t3_compiled_renderer_t;

typedef struct {
    /* int32 retains +32768 when conjugating a signed -32768 coefficient. */
    int32_t joint[T3_PSX_VISIBLE_PARTS][12];
    int32_t center_x, center_z;
} t3_compiled_pose_t;

/* Load-time validation; a rejected asset keeps the complete reference path. */
int t3_compiled_renderer_init(t3_compiled_renderer_t* renderer,
    const t3_compiled_plan_t* plan, const sat_skin_asset_t* skin,
    const uint16_t* indices, uint16_t face_count, const uint8_t* double_sided);

/* Preflight every referenced joint before writing. No per-vertex guards or
 * matrix expansion in the range kernel. A failure leaves output untouched. */
int t3_compiled_pose_pack(const t3_compiled_renderer_t* renderer,
    const t3_psx_joint_t* original, uint32_t valid_parts,
    int32_t center_x, int32_t center_z, t3_compiled_pose_t* output);

/* Caller retains the validated immutable renderer and a successfully packed
 * pose until every disjoint range has completed. Outputs use absolute IDs. */
int t3_compiled_deform_range(const t3_compiled_renderer_t* renderer,
    const t3_compiled_pose_t* pose, uint32_t first, uint32_t count,
    sat_vec3_t* output, uint32_t capacity);

#endif
