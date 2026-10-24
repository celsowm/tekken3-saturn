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
/* t3_motion_open without the knot-span sums: the header and key bounds are
 * checked, so frames and counts are safe to read, but the stream must pass
 * t3_motion_open before it is sampled. */
int t3_motion_open_header(const t3_divmot_t* character,
                          const t3_divmot_t* common,
                          uint32_t animation_word, t3_motion_stream_t* out);

/* Caller-owned single-entry cache. Banks and their bytes must be immutable
 * while cached; zero the cache when reloading/modifying a bank. Only successful
 * opens are cached. The returned view belongs to cache, until its next miss. */
typedef struct {
    const t3_divmot_t *bank,*common;
    uint32_t word;
    uint8_t valid;
    t3_motion_stream_t stream;
} t3_motion_cache_t;
int t3_motion_open_cached(t3_motion_cache_t* cache,const t3_divmot_t* bank,
                          const t3_divmot_t* common,uint32_t word,
                          const t3_motion_stream_t** out);

/* Decode the original 49 scalar pose channels (including XYZ displacement),
 * already doubled and 16-bit wrapped. Frame is clamped to N-1.
 * 1 success; 0 = requires missing spline table; -1 bad input/tables.
 * No malloc, libm, floating point or unbounded stream search.
 */
int t3_motion_sample(const t3_motion_stream_t* stream, unsigned frame,
                     const t3_motion_tables_t* tables,
                     int16_t pose[T3_PSX_POSE_CHANNELS]);

/* Where one spline channel's knot walk last stopped; next 0 means none. */
typedef struct {
    int32_t p0,p1,p2;
    uint16_t prev,cur;
    uint8_t next;
} t3_motion_knot_t;
typedef struct {
    const uint8_t* stream; /* the stream's first key; NULL when unused */
    uint8_t frames;
    t3_motion_knot_t knot[T3_PSX_POSE_CHANNELS];
} t3_motion_cursor_t;
/* t3_motion_sample, with each spline channel's knot walk resumed where the
 * cursor's last sample of the same stream stopped, so a frame near the last
 * costs a step or two rather than a walk from the first knot. The results
 * are the same. A cursor holding another stream restarts. Caller-owned
 * derived data: set stream to NULL after editing bank bytes. A NULL cursor
 * samples without one. */
int t3_motion_sample_cursor(const t3_motion_stream_t* stream, unsigned frame,
                            const t3_motion_tables_t* tables,t3_motion_cursor_t* cursor,
                            int16_t pose[T3_PSX_POSE_CHANNELS]);

/* Camera section-8 spline segment: the same original basis, shift 4 rather
 * than a pose channel's shift 1/5. Keys are little-endian signed words. */
int t3_motion_spline_words(const uint8_t* keys,size_t bytes,unsigned count,
                           unsigned frame,unsigned frames,const t3_motion_tables_t* tables,
                           int16_t* out);

enum {T3_MOTION_SAMPLE_CACHE_SLOTS=512};
typedef struct {
    const uint8_t* stream;
    uint16_t frame;
    uint8_t frames,valid;
    int16_t pose[T3_PSX_POSE_CHANNELS];
} t3_motion_sample_entry_t;
typedef struct {
    const uint8_t* table;
    size_t table_length;
    uint32_t hits,misses;
    t3_motion_sample_entry_t entries[T3_MOTION_SAMPLE_CACHE_SLOTS];
} t3_motion_sample_cache_t;
/* Optional caller-owned derived-data cache, outside the match transaction.
 * Reset before first use and after reloading/modifying bank bytes, stream
 * descriptors or table bytes. All are immutable while cached. Changing the
 * table pointer/length invalidates automatically. Failed samples never cache
 * an unavailable/invalid result. Cache counters are not simulation state. */
void t3_motion_sample_cache_reset(t3_motion_sample_cache_t* cache);
int t3_motion_sample_cached(t3_motion_sample_cache_t* cache,
    const t3_motion_stream_t* stream,unsigned frame,
    const t3_motion_tables_t* tables,int16_t pose[T3_PSX_POSE_CHANNELS]);

/* Sample only channels 0..2. If they are simple/non-spline, the original
 * EXE spline tables are not needed; otherwise returns 0. */
int t3_motion_sample_root(const t3_motion_stream_t* stream, unsigned frame,
                          const t3_motion_tables_t* tables,
                          int16_t xyz[3]);

#endif
