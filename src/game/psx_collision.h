#ifndef T3_PSX_COLLISION_H
#define T3_PSX_COLLISION_H

#include <stddef.h>
#include <stdint.h>
#include "attack_records.h"

/* PS1 coordinates, 32-bit integers. Q12 sin/cos are supplied by the caller
 * so the portable core does not depend on LibSaturn, libm or floating point.
 * 'sin_q12'/'cos_q12' rotate local XZ into world XZ. */
enum { T3_PSX_HURT_ZONES=14u, T3_PSX_JOINT_COUNT=24u, T3_PSX_UNIT_LIMIT=1000000 };
typedef struct { int32_t x,y,z; } t3_vec3i_t;
typedef struct {
    t3_vec3i_t origin;
    int16_t sin_q12, cos_q12;
} t3_psx_root_t;
typedef struct {
    t3_vec3i_t start,end;
    uint8_t previous_start;
} t3_world_segment_t;
typedef struct {
    t3_world_segment_t segments[2];
    uint8_t segment_count;
} t3_world_attack_t;
typedef struct {
    t3_vec3i_t center;
    uint16_t radius; /* PS1 cylinders extend +/- radius vertically AND in XZ */
} t3_hurt_cylinder_t;

/* Convert baked attack points using current and previous frame roots.
 * For swept starts, previous_root is required. The segment remains in the
 * original PS1 coordinate basis (no Saturn Y-axis conversion).
 * Returns 1 on success, 0 invalid input / transform out of range. */
int t3_attack_to_world(const t3_attack_sample_t* input,
                       const t3_psx_root_t* current_root,
                       const t3_psx_root_t* previous_root,
                       t3_world_attack_t* out);

/* Fixed-point segment/cylinder intersection; no float, no heap, bounded
 * 64-bit arithmetic. 1 = intersects, 0 = misses, -1 = malformed data.
 * Narrow touching cases within a 16.16 param step are approximate until
 * compared with the original PS1 integer collision routine. */
int t3_segment_hits_hurt(const t3_world_segment_t* segment,
                         const t3_hurt_cylinder_t* cylinder);

/* Caller must supply ACTUAL PS1 world-space joint translations, in the
 * original order (not GLB/LibSaturn skin joint order).
 * 'large' selects the documented alternate radius profile; some characters
 * have specific head exceptions not yet available here.
 * These generated zones are NOT authoritative without original poses. */
int t3_hurt_from_psx_joints(const t3_vec3i_t joints[T3_PSX_JOINT_COUNT],
                            int large,
                            t3_hurt_cylinder_t out[T3_PSX_HURT_ZONES]);

/* Returns first zone index with a hit, -1 = no hit, -2 = invalid input.
 * Segment order 0,1; zone order 0..zone_count-1. */
int t3_attack_hits_zones(const t3_world_attack_t* attack,
                          const t3_hurt_cylinder_t* zones,
                          size_t zone_count,
                          unsigned* segment_index);

#endif
