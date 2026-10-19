#ifndef T3_PSX_LOCAL_POSE_H
#define T3_PSX_LOCAL_POSE_H
#include "psx_ik.h"

typedef struct {
    int16_t pose[T3_PSX_POSE_CHANNELS];
    t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS];
    uint32_t fingerprint,fk_mask,mask;
    int16_t root_xx;
    uint8_t valid;
} t3_psx_local_pose_entry_t;
typedef struct {
    const uint8_t *fk_trig,*ik_trig,*ik_blob;
    size_t fk_size,ik_trig_size,ik_size;
    t3_psx_local_pose_entry_t* entries;
    unsigned capacity,cursor;
    uint32_t hits,misses,evictions;
} t3_psx_local_pose_cache_t;

/* Derived, caller-owned data outside the match transaction. Only the raw local
 * FK/IK result is cached: blend, tracking, placement and contacts stay live.
 * Table bytes must stay immutable. Initialize before first use and again after
 * editing/reloading bytes at the same address. Pointer/size changes invalidate
 * automatically. Storage may not alias pose/output. No allocation or threading.
 * A NULL cache computes the same operation without retaining a result. */
void t3_psx_local_pose_cache_init(t3_psx_local_pose_cache_t* cache,
    t3_psx_local_pose_entry_t* entries,unsigned capacity);
int t3_psx_local_pose(const int16_t pose[T3_PSX_POSE_CHANNELS],
    const t3_psx_trig_t* fk_trig,const t3_psx_ik_tables_t* ik,
    t3_psx_local_pose_cache_t* cache,
    t3_psx_matrix_t out[T3_PSX_ROTATION_SLOTS],
    uint32_t* fk_mask,uint32_t* mask,int16_t* root_xx);
#endif
