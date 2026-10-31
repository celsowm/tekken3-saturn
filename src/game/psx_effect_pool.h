#ifndef T3_PSX_EFFECT_POOL_H
#define T3_PSX_EFFECT_POOL_H
#include "psx_spark.h"
#include "psx_flipbook.h"
enum { T3_PSX_EFFECT_CAPACITY=80,T3_PSX_EFFECT_NONE=80,T3_PSX_EFFECT_TABLE_BYTES=681,
       T3_PSX_EFFECT_BREATH_OFFSETS=440,T3_PSX_EFFECT_FLAME_OFFSETS=472,T3_PSX_EFFECT_CLOUD_OFFSETS=488,
       T3_PSX_EFFECT_GAS_OFFSETS=504,T3_PSX_EFFECT_BURN_JOINTS=520,T3_PSX_EFFECT_GAS_TIMES=560,
       T3_PSX_EFFECT_GAS_ENTRIES=9,T3_PSX_EFFECT_SEGMENT_FRAMES=616 };
typedef struct {
    t3_psx_spark_t object[80];
    uint8_t next[80],previous[80],head,tail,free_head,free_tail,count;
    uint8_t replay_glows,end_cleared,burn_flags;
    uint32_t counter,live,glow_mask[2];
    t3_psx_flipbook_pool_t flipbook; /* hit bursts and character flipbooks (state only) */
} t3_psx_effect_pool_t;
typedef struct {
    uint8_t active,character,joints[4];
    uint8_t first_joint,hit_clean,guarded;
    int16_t power_timer,pose,active_first,active_last;
    const t3_psx_joint_t* world;
} t3_psx_effect_actor_t;
void t3_psx_effect_pool_round(t3_psx_effect_pool_t* pool);
/* Keeps original slot/free/live ordering and retained object bytes. */
t3_psx_spark_t* t3_psx_effect_alloc(t3_psx_effect_pool_t* pool,unsigned type);
/* Optional private tables: 26 u32 glow joints + 21 pairs of SVECTORs.
 * Required only when an active power glow uses them. Unknown effect types
 * fail rather than silently omitting their simulation/RNG. */
int t3_psx_effect_pool_step(t3_psx_effect_pool_t* pool,const t3_psx_trig_t* trig,
    const uint8_t* tables,size_t bytes,const t3_psx_effect_actor_t actors[2],
    uint32_t* random,unsigned buffer,int enabled,int replay,int camera_phase);
/* Spawn already classified normal/power hit sparks. The caller supplies
 * the original hit-slot endpoint and effect-space pitch/yaw. */
void t3_psx_effect_spray(t3_psx_effect_pool_t* pool,t3_vec3i_t point,int16_t pitch,int16_t yaw,int power);
#endif
