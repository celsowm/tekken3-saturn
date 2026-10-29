#ifndef T3_PSX_SPARK_H
#define T3_PSX_SPARK_H
#include "psx_pose.h"

/* Canonical spark state; no PS1 pointers or double-buffered draw primitives.
 * Models: 0 unset, 1 normal spark, 2 power spark. Both CLUTs are retained. */
typedef struct {
    uint8_t type,flags,state,model;
    int16_t pitch,yaw,speed;
    uint16_t clut[2];
    int32_t x,y,z,vx,vy,vz,a,b,c,d;
} t3_psx_spark_t;
/* Original object types 1/2/12/13. One update, including initialization on
 * state 0. Floor/age expiry changes the object to type 0, state 0, flags 0x80;
 * caller must subsequently run the pool's two-update placeholder/free rule.
 * Returns 0 for unsupported type or missing tables, 1 for a completed call.
 * Allocation, hit spawning, replay logs and draw submission belong to caller. */
int t3_psx_spark_step(t3_psx_spark_t* spark,const t3_psx_trig_t* trig,
                       uint32_t* random,unsigned display_buffer);
#endif
