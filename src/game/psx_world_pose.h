#ifndef T3_PSX_WORLD_POSE_H
#define T3_PSX_WORLD_POSE_H
#include "psx_pose.h"

typedef struct {
    const t3_psx_part_t* parts;
    uint32_t fingerprint,slots_mask,parts_mask,stamp;
    t3_psx_matrix_t root,slots[T3_PSX_ROTATION_SLOTS];
    t3_psx_joint_t relative[T3_PSX_DRAW_PARTS];
    uint8_t valid;
} t3_psx_world_pose_entry_t;
typedef struct {
    t3_psx_world_pose_entry_t* entries;
    unsigned capacity;
    uint32_t hits,misses,evictions,clock;
} t3_psx_world_pose_cache_t;

/* Caller-owned, exclusively accessed derived storage. Capacity must be a
 * power of two >=4; other capacities disable retention. Parts and their bytes
 * must remain immutable until reinitialization. Storage must not alias inputs
 * or outputs. Keys include all valid local matrices AFTER blend/tracking and
 * the root rotation. Every hit checks the full key, not just a fingerprint.
 * Root translation is added to exact relative origins with original bounds.
 * Failure leaves outputs untouched; derived statistics may still advance. */
void t3_psx_world_pose_cache_init(t3_psx_world_pose_cache_t* cache,
    t3_psx_world_pose_entry_t* entries,unsigned capacity);
int t3_psx_compose_cached(const t3_psx_part_t parts[T3_PSX_DRAW_PARTS],
    const t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],uint32_t valid_slots,
    const t3_psx_joint_t* root,t3_psx_world_pose_cache_t* cache,
    t3_psx_joint_t out[T3_PSX_DRAW_PARTS],uint32_t* valid_parts);
#endif
