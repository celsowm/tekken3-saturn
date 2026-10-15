#include "psx_collision.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static t3_vec3i_t vec(int x,int y,int z) {
    t3_vec3i_t v={(int32_t)x,(int32_t)y,(int32_t)z};return v;
}
static t3_world_segment_t line(t3_vec3i_t a,t3_vec3i_t b) {
    t3_world_segment_t s={0};s.start=a;s.end=b;return s;
}
static t3_hurt_cylinder_t zone(int x,int y,int z,int r) {
    t3_hurt_cylinder_t c={0};c.center=vec(x,y,z);c.radius=(uint16_t)r;
    return c;
}
static void transform_tests(void) {
    t3_attack_sample_t local={0};
    t3_world_attack_t world={0};
    t3_psx_root_t cur={0},prev={0};
    local.segment_count=2u;
    local.segments[0].start=(t3_attack_point_t){100,-50,0};
    local.segments[0].end=(t3_attack_point_t){300,-50,0};
    local.segments[1].start=(t3_attack_point_t){100,0,0};
    local.segments[1].end=(t3_attack_point_t){0,200,100};
    local.segments[1].previous_start=1u;
    cur.origin=vec(10000,0,200);cur.cos_q12=4096;
    prev.origin=vec(5000,0,0);prev.sin_q12=4096;
    assert(t3_attack_to_world(&local,&cur,&prev,&world));
    assert(world.segment_count==2);
    assert(world.segments[0].start.x==10100);
    assert(world.segments[0].start.y==-50);
    assert(world.segments[0].start.z==200);
    assert(world.segments[0].end.x==10300);
    assert(world.segments[1].previous_start==1);
    assert(world.segments[1].start.x==5000);
    assert(world.segments[1].start.z==100);
    assert(world.segments[1].end.x==10000);
    assert(world.segments[1].end.y==200);
    assert(world.segments[1].end.z==300);
    assert(!t3_attack_to_world(&local,&cur,NULL,&world));
    cur.cos_q12=0;cur.sin_q12=4096;
    local.segment_count=1;
    assert(t3_attack_to_world(&local,&cur,NULL,&world));
    assert(world.segments[0].start.x==10000);
    assert(world.segments[0].start.z==300);
    assert(world.segments[0].end.z==500);
    cur.sin_q12=5000;
    assert(!t3_attack_to_world(&local,&cur,NULL,&world));
    cur.sin_q12=4096;cur.origin.x=1000000;
    assert(!t3_attack_to_world(&local,&cur,NULL,&world));
    local.segment_count=3;
    assert(!t3_attack_to_world(&local,&cur,NULL,&world));
}
static void cylinder_tests(void) {
    t3_hurt_cylinder_t c=zone(0,0,0,200);
    t3_world_segment_t s=line(vec(-500,0,0),vec(500,0,0));
    assert(t3_segment_hits_hurt(&s,&c)==1);
    s=line(vec(-500,0,200),vec(500,0,200));
    assert(t3_segment_hits_hurt(&s,&c)==1); /* tangent */
    s=line(vec(-500,0,201),vec(500,0,201));
    assert(t3_segment_hits_hurt(&s,&c)==0);
    s=line(vec(0,201,0),vec(0,500,0));
    assert(t3_segment_hits_hurt(&s,&c)==0);
    s=line(vec(0,200,0),vec(0,500,0));
    assert(t3_segment_hits_hurt(&s,&c)==1);
    s=line(vec(0,-500,0),vec(0,500,0));
    assert(t3_segment_hits_hurt(&s,&c)==1);
    s=line(vec(199,-500,0),vec(199,500,0));
    assert(t3_segment_hits_hurt(&s,&c)==1);
    s=line(vec(201,-500,0),vec(201,500,0));
    assert(t3_segment_hits_hurt(&s,&c)==0);
    s=line(vec(0,-500,201),vec(0,500,201));
    assert(t3_segment_hits_hurt(&s,&c)==0);
    s=line(vec(100,100,100),vec(100,100,100));
    assert(t3_segment_hits_hurt(&s,&c)==1);
    s=line(vec(201,100,0),vec(201,100,0));
    assert(t3_segment_hits_hurt(&s,&c)==0);
    c.radius=0;
    assert(t3_segment_hits_hurt(&s,&c)==0);
    s=line(vec(1000001,0,0),vec(0,0,0));
    assert(t3_segment_hits_hurt(&s,&c)==-1);
    assert(t3_segment_hits_hurt(NULL,&c)==-1);
}
static void ps1_zones(void) {
    t3_vec3i_t joints[T3_PSX_JOINT_COUNT]={0};
    t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES]={0};
    unsigned i;
    const int mapping[]={17,14,10,6,16,13,9,5,2,8,4,0,15,12};
    const int radius[]={200,200,120,120,300,300,100,100,250,300,300,200,400,400};
    for(i=0;i<T3_PSX_JOINT_COUNT;++i)joints[i]=vec(100+(int)i*25,(int)i*70,0);
    assert(t3_hurt_from_psx_joints(joints,0,zones));
    for(i=0;i<14;++i) {
        assert(zones[i].center.x==joints[mapping[i]].x);
        assert(zones[i].radius==radius[i]);
    }
    assert(zones[8].center.y==joints[2].y-120);
    assert(zones[11].center.y==joints[0].y-60);
    assert(t3_hurt_from_psx_joints(joints,1,zones));
    assert(zones[8].radius==450);
    assert(zones[13].radius==600);
    assert(zones[11].radius==400);
    joints[0].x=T3_PSX_UNIT_LIMIT+1;
    assert(!t3_hurt_from_psx_joints(joints,0,zones));
}
static void zone_tests(void) {
    t3_world_attack_t a={0};
    t3_hurt_cylinder_t zones[3]={
        {{5000,0,0},200},{{0,0,0},150},{{500,0,0},150}
    };
    unsigned seg=100;
    a.segment_count=2;
    a.segments[0]=line(vec(1000,0,0),vec(700,0,0));
    a.segments[1]=line(vec(1000,0,0),vec(0,0,0));
    assert(t3_attack_hits_zones(&a,zones,3,&seg)==1);
    assert(seg==1);
    zones[1].radius=0;
    assert(t3_attack_hits_zones(&a,zones,3,&seg)==2);
    assert(seg==1);
    zones[2].radius=0;
    assert(t3_attack_hits_zones(&a,zones,3,&seg)==-1);
    a.segment_count=3;
    assert(t3_attack_hits_zones(&a,zones,3,&seg)==-2);
    a.segment_count=1;
    assert(t3_attack_hits_zones(&a,zones,15,&seg)==-2);
}
int main(void) {
    transform_tests();
    cylinder_tests();
    ps1_zones();
    zone_tests();
    puts("PS1 integer world attack transforms and 14 hurt zones: passed");
    return 0;
}
