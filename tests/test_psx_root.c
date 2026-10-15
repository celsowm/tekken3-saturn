#include "psx_root.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t trig_bytes[T3_PSX_TRIG_BYTES];
static void put16(unsigned p,int value) {
    trig_bytes[p]=(uint8_t)value;
    trig_bytes[p+1u]=(uint8_t)((uint16_t)value>>8u);
}
static void setup(void) {
    memset(trig_bytes,0,sizeof(trig_bytes));
    put16(0,0);
    put16(0x800,4096);
    put16(0x1000,0);
    put16(0x1800,-4096);
    put16(0x2000,0);
}
static void test_world_root(void) {
    t3_psx_trig_t trig={trig_bytes,sizeof(trig_bytes)};
    t3_psx_joint_t out={0},prior;
    int16_t pose[T3_PSX_POSE_CHANNELS]={0};
    t3_vec3i_t anchor={1000,2000,-1000};
    setup();
    pose[0]=100;pose[1]=-200;pose[2]=300;
    assert(t3_psx_pose_world_root(pose,100,anchor,0x8000,0x8000,&trig,&out));
    assert(out.origin.x==1100 && out.origin.y==1800 &&
           out.origin.z==-700);
    assert(out.rotation.m[0][0]==4096);
    assert(out.rotation.m[0][2]==0);
    assert(out.rotation.m[1][1]==4096);
    assert(out.rotation.m[2][2]==4096);

    assert(t3_psx_pose_world_root(pose,50,anchor,0x8000,0x8000,&trig,&out));
    assert(out.origin.x==1050 && out.origin.y==1900 &&
           out.origin.z==-850);
    assert(t3_psx_pose_world_root(pose,100,anchor,0x4000,0xC000,&trig,&out));
    assert(out.origin.x==700 && out.origin.y==1800 &&
           out.origin.z==-900);
    assert(out.rotation.m[0][0]==0);
    assert(out.rotation.m[0][2]==4096);
    assert(out.rotation.m[2][0]==-4096);

    /* s16 root displacement wraps AFTER the Q12 KMD scale operation. */
    pose[0]=30000;pose[1]=0;pose[2]=0;
    anchor=(t3_vec3i_t){0,0,0};
    assert(t3_psx_pose_world_root(pose,200,anchor,0x8000,0x8000,&trig,&out));
    assert(out.origin.x==-5536);
    prior=out;
    assert(!t3_psx_pose_world_root(pose,0,anchor,0x8000,0x8000,&trig,&out));
    assert(!memcmp(&prior,&out,sizeof(prior)));
    assert(!t3_psx_pose_world_root(pose,201,anchor,0x8000,0x8000,&trig,&out));
    trig.size=1234;
    assert(!t3_psx_pose_world_root(pose,100,anchor,0x8000,0x8000,&trig,&out));
    trig.size=sizeof(trig_bytes);
    anchor.x=T3_PSX_UNIT_LIMIT+1;
    assert(!t3_psx_pose_world_root(pose,100,anchor,0x8000,0x8000,&trig,&out));
    assert(!memcmp(&prior,&out,sizeof(prior)));
}
int main(void) {
    test_world_root();
    puts("PS1 world root scale/anchor/facing/heading fixed-point tests: passed");
    return 0;
}
