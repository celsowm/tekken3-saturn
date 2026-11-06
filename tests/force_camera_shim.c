/* ctypes entry for scripts/check_force_flow_trace.py: one camera frame on the record's 14 words. */
#include "psx_force_camera.h"
#include <string.h>
void force_camera_step(int32_t* rec,const uint8_t* trig,int32_t roll,int32_t px,int32_t pz){
    t3_force_camera_t c;
    c.player=(uint32_t)rec[0];
    memcpy(&c.x,rec+1,13*sizeof(int32_t));
    t3_force_camera_frame(&c,trig,roll,px,pz);
    rec[0]=(int32_t)c.player;
    memcpy(rec+1,&c.x,13*sizeof(int32_t));
}
