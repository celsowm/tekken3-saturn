#ifndef T3_PSX_IK_H
#define T3_PSX_IK_H

#include <stddef.h>
#include <stdint.h>
#include "psx_pose.h"

/* Normalized private table image, independent of region-specific EXE VAs:
 * 0x000..0x17F 192 signed PS1 SquareRoot0 mantissa words (index 0x40..0xFF)
 * 0x180..0x191 nine signed Q12 limb basis matrix entries (row-major)
 * 0x192..0x1C1 four signed XYZ skeleton vectors, 12 bytes each
 * 0x1C2..0x1F1 four signed XYZ limb root offsets, 12 bytes each.
 * Derived from the author's own disc; NEVER ship real table bytes. */
enum { T3_PSX_SQRT_BYTES=0x180, T3_PSX_IK_BYTES=0x1F2 };

typedef struct {
    t3_psx_trig_t trig;
    const uint8_t* ik_blob;
    size_t ik_size;
} t3_psx_ik_tables_t;

/* Apply PS1 basis transforms, then run all four original two-bone solvers.
 * Ten Euler FK slots are supplied by the caller. Successful limbs add
 * (4,5), (8,9), (12,13), and (15,16) to the validity mask.
 * 1: valid source tables and finite values (mask records unsolved limbs);
 * 0: missing tables; -1: corrupt tables / unsupported numerical state.
 * Outputs remain unchanged on 0/-1; game damage is NEVER driven here. */
int t3_psx_pose_ik(const int16_t pose[T3_PSX_POSE_CHANNELS],
                   const t3_psx_ik_tables_t* table,
                   const t3_psx_matrix_t input[T3_PSX_ROTATION_SLOTS],
                   uint32_t input_mask,
                   t3_psx_matrix_t output[T3_PSX_ROTATION_SLOTS],
                   uint32_t* output_mask);

/* One-call 18-slot path. Does not invent any limb on failed reach. */
int t3_psx_pose_full(const int16_t pose[T3_PSX_POSE_CHANNELS],
                     const t3_psx_ik_tables_t* table,
                     t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],
                     uint32_t* valid_slots);

#endif
