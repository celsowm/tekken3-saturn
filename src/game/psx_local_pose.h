#ifndef T3_PSX_LOCAL_POSE_H
#define T3_PSX_LOCAL_POSE_H
#include "psx_ik.h"

/* Field order packs the small key bytes into the padding after slots. */
typedef struct {
    int16_t pose[T3_PSX_POSE_CHANNELS];
    t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS];
    uint8_t frame,frames;
    const uint8_t* stream; /* the stream's first key; NULL means empty */
    uint32_t fk_mask,mask;
    int16_t root_xx;
    uint8_t recent; /* a set's first way records the way used last */
} t3_psx_local_pose_entry_t;
/* Knot cursors for the streams sampled most recently: both fighters' moves
 * and a blend source. */
enum { T3_PSX_LOCAL_POSE_CURSORS=4 };
typedef struct {
    const uint8_t *fk_trig,*ik_trig,*ik_blob,*spline;
    size_t fk_size,ik_trig_size,ik_size,spline_size;
    t3_psx_local_pose_entry_t* entries;
    unsigned capacity,sets,ways,set_mask;
    uint32_t hits,misses,evictions;
    uint32_t cursor_used[T3_PSX_LOCAL_POSE_CURSORS],cursor_clock;
    t3_motion_cursor_t cursor[T3_PSX_LOCAL_POSE_CURSORS];
} t3_psx_local_pose_cache_t;

/* Derived, caller-owned data outside the match transaction: one stream
 * frame's sampled pose and its raw local FK/IK result. Blend, tracking,
 * placement and contacts stay live. Bank and table bytes must stay immutable.
 * Initialize before first use and again after editing/reloading bytes at the
 * same address; table pointer/size changes invalidate automatically.
 * An even capacity of two or more is two-way set-associative, with
 * consecutive frames of a stream in consecutive sets. A miss samples with a
 * knot cursor (t3_motion_sample_cursor) of the same stream, or the least
 * recently used one. No allocation or threading. A NULL cache computes the
 * same operation without retaining it. */
void t3_psx_local_pose_cache_init(t3_psx_local_pose_cache_t* cache,
    t3_psx_local_pose_entry_t* entries,unsigned capacity);
/* Sample the stream at frame (clamped to N-1), then FK and IK. Returns 1, or
 * 0 with the outputs unchanged when sampling or the solver fails. */
int t3_psx_local_pose(const t3_motion_stream_t* stream,unsigned frame,
    const t3_motion_tables_t* spline,
    const t3_psx_trig_t* fk_trig,const t3_psx_ik_tables_t* ik,
    t3_psx_local_pose_cache_t* cache,int16_t pose[T3_PSX_POSE_CHANNELS],
    t3_psx_matrix_t out[T3_PSX_ROTATION_SLOTS],
    uint32_t* fk_mask,uint32_t* mask,int16_t* root_xx);
#endif
