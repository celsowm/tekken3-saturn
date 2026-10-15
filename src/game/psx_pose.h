#ifndef T3_PSX_POSE_H
#define T3_PSX_POSE_H

#include <stddef.h>
#include <stdint.h>
#include "psx_motion.h"
#include "psx_collision.h"

enum {
    T3_PSX_ROTATION_SLOTS = 18,
    T3_PSX_DRAW_PARTS = 22,
    T3_PSX_TRIG_BYTES = 0x2800
};
/* The private PS1 sine table starts at reference address 0x8001E8C4.
 * Both sin(angle) and cos(angle) are read from this same lookup array,
 * with cos shifted by 0x800 bytes (not assuming another region's address).
 * 0x2800 bytes cover both high-angle sine and offset cosine lookups. */
typedef struct {
    const uint8_t* data;
    size_t size;
} t3_psx_trig_t;
typedef struct {
    int16_t m[3][3];  /* 4.12 PS1 rotation, row-major */
} t3_psx_matrix_t;
typedef struct {
    int8_t parent;       /* -1 means character root; otherwise preceding draw part */
    uint8_t slot;        /* PS1 animation/attachment slot 0..21; 18+ pending */
    t3_vec3i_t offset;   /* PS1 model's (x, y, -z), not normalized skeleton */
} t3_psx_part_t;
typedef struct {
    t3_psx_matrix_t rotation;
    t3_vec3i_t origin;
} t3_psx_joint_t;

/* Builds ONLY 10 original FK Euler matrices from channels 19..48.
 * Returns a bitmask of VALID slots. IK slots remain invalid; they MUST be
 * supplied by the original limb solver before full character composition.
 * On failure, neither outputs nor mask are modified. */
int t3_psx_pose_fk(const int16_t pose[T3_PSX_POSE_CHANNELS],
                   const t3_psx_trig_t* table,
                   t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],
                   uint32_t* valid_slots);

/* Q12 matrix multiplication, saturating signed 16-bit like the PS1 GTE
 * MVMVA sf=1 lm=0. Can be used for FK joint chains. */
int t3_psx_mul_rot(const t3_psx_matrix_t* a, const t3_psx_matrix_t* b,
                   t3_psx_matrix_t* out);

/* Compose the REAL model's parent/offset table for all 22 draw parts.
 * Missing local IK slots invalidate each affected subtree. No identity
 * fallback is applied. 'root' supplies fighter world position and heading.
 * Returns 1 success or 0 on malformed parents/unsafe coordinates; results
 * are published only on success. */
int t3_psx_compose_parts(const t3_psx_part_t parts[T3_PSX_DRAW_PARTS],
                         const t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],
                         uint32_t valid_slots, const t3_psx_joint_t* root,
                         t3_psx_joint_t out[T3_PSX_DRAW_PARTS],
                         uint32_t* valid_parts);

/* Number of available draw parts, useful for Saturn debug telemetry. */
unsigned t3_psx_count_parts(uint32_t mask);

#endif
