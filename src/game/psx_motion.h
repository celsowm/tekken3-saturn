#ifndef T3_PSX_MOTION_H
#define T3_PSX_MOTION_H

#include <stddef.h>
#include <stdint.h>
#include "divmot.h"

enum {
    T3_PSX_POSE_CHANNELS = 49,
    T3_PSX_SPLINE_TABLE_BYTES = 0x660
};

/* Zero-copy animation stream view. Section 6 of DIVMOT contains one or
 * more 50-byte stream headers followed by signed 16-bit channel keys.
 * Even channels sample in chronological order; odd channels are reversed.
 * This is a PS1 pose vector, NOT directly usable as LibSaturn skin joints. */
typedef struct {
    const uint8_t* data;
    size_t length;
    uint8_t frames;
    uint8_t counts[T3_PSX_POSE_CHANNELS];
    uint32_t key_offset[T3_PSX_POSE_CHANNELS];
    int16_t tag;
    uint8_t bank_type;
} t3_motion_stream_t;

/* The byte-for-byte PS1 spline basis/index tables are private EXE data.
 * blob[0] maps to PS1 virtual address 0x80096B00 in the Japan Rev.1
 * research layout. This is a NORMALIZED table layout, not a claim that
 * the USA ROM stores these bytes at the same virtual address. */
typedef struct {
    const uint8_t* blob;
    size_t length;
} t3_motion_tables_t;

/* Return 1 success, 0 absent animation section/data, -1 invalid bounds or
 * malformed motion stream. Never dereference beyond the selected bank.
 * Shared animation word 0x40000000+offset resolves in divmot99. */
int t3_motion_open(const t3_divmot_t* character,
                   const t3_divmot_t* common,
                   uint32_t animation_word, t3_motion_stream_t* out);

/* Decode the original 49 scalar pose channels (including XYZ displacement),
 * already doubled and 16-bit wrapped. Frame is clamped to N-1.
 * 1 success; 0 = requires missing spline table; -1 bad input/tables.
 * No malloc, libm, floating point or unbounded stream search.
 */
int t3_motion_sample(const t3_motion_stream_t* stream, unsigned frame,
                     const t3_motion_tables_t* tables,
                     int16_t pose[T3_PSX_POSE_CHANNELS]);

/* Sample only channels 0..2. If they are simple/non-spline, the original
 * EXE spline tables are not needed; otherwise returns 0. */
int t3_motion_sample_root(const t3_motion_stream_t* stream, unsigned frame,
                          const t3_motion_tables_t* tables,
                          int16_t xyz[3]);

#endif
