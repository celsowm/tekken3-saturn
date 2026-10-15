#ifndef T3_PSX_TRACE_H
#define T3_PSX_TRACE_H

#include <stddef.h>
#include <stdint.h>
#include "psx_pose.h"

/* Portable byte layout for PS1 pose-oracle comparisons; always little-endian,
 * including on the Saturn's big-endian SH-2 CPU. Never serialise C structs.
 *
 * Header bytes (24):
 * 0  "T3PX", 4 u16 version, 6 u16 joint count (=18), 8 u32 tick,
 * 12 u16 move slot, 14 u16 pose frame, 16 u16 fighter, 18 u16 reserved,
 * 20 u32 valid part mask.
 * Per joint in ascending original PS1 KMD draw-part order (30 bytes):
 * origin i32[3], 3x3 row-major Q12 rotation i16[9].
 * Invalid parts encode as ZERO, regardless of caller memory contents.
 *
 * This format deliberately stores no Namco authored vertices, textures,
 * animations or coefficient tables. Captured pose traces are nevertheless
 * private game-derived diagnostics and must NOT be committed. */
enum {
    T3_PSX_TRACE_JOINTS=18,
    T3_PSX_TRACE_HEADER=24,
    T3_PSX_TRACE_JOINT_BYTES=30,
    T3_PSX_TRACE_BYTES=24+18*30,
    T3_PSX_TRACE_VERSION=1
};
#define T3_PSX_TRACE_BODY_MASK ((1u<<T3_PSX_TRACE_JOINTS)-1u)

int t3_psx_trace_encode(uint32_t tick, unsigned fighter,
                        uint16_t move_slot, uint16_t pose_frame,
                        const t3_psx_joint_t parts[T3_PSX_DRAW_PARTS],
                        uint32_t valid_mask,
                        uint8_t* output, size_t capacity);

#endif
