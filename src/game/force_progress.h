#ifndef T3_FORCE_PROGRESS_H
#define T3_FORCE_PROGRESS_H
#include <stdint.h>
#include "psx_force_handoff.h"
#include "psx_force_level.h"

/* Original Force progress: saved keys saturate at three, the high score, and
 * Doctor B.'s unlock. The Saturn record is independent of the PS1 save layout. */
#define T3_FORCE_PROGRESS_BYTES 16u
typedef struct { uint32_t hiscore; uint8_t keys,doctor; } t3_force_progress_t;
int t3_force_progress_pack(const t3_force_progress_t* in,uint8_t out[T3_FORCE_PROGRESS_BYTES]);
int t3_force_progress_unpack(const uint8_t* in,uint32_t bytes,t3_force_progress_t* out);

/* FUN_800B61D4, adapted from Tekken3Remake mode_rules.gd with attribution in
 * force_progress.c. A next level is available only after a won, ended level. */
enum { T3_FORCE_RUN_LOST=0,T3_FORCE_RUN_NEXT=1,T3_FORCE_RUN_RESULT=2 };
int t3_force_run_finish(const t3_force_handoff_t* start,const t3_force_level_t* level,
    int won,int32_t health,uint32_t seed,t3_force_handoff_t* out);
#endif
