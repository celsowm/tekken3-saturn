#include "psx_contact.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static t3_vec3i_t point(int x,int y,int z) {
    t3_vec3i_t v={(int32_t)x,(int32_t)y,(int32_t)z};return v;
}
static void setup_parts(t3_psx_joint_t parts[T3_PSX_DRAW_PARTS],int dx) {
    unsigned i;
    memset(parts,0,sizeof(t3_psx_joint_t)*T3_PSX_DRAW_PARTS);
    for(i=0;i<T3_PSX_DRAW_PARTS;++i) {
        parts[i].origin=point(dx+(int)i*5,(int)i*10,100);
        parts[i].rotation.m[0][0]=4096;
        parts[i].rotation.m[1][1]=4096;
        parts[i].rotation.m[2][2]=4096;
    }
}
static void zones_from_pose(void) {
    t3_psx_joint_t parts[T3_PSX_DRAW_PARTS];
    t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES];
    t3_hurt_cylinder_t old[T3_PSX_HURT_ZONES];
    const uint8_t map[14]={17,14,10,6,16,13,9,5,2,8,4,0,15,12};
    const uint16_t radius[14]={200,200,120,120,300,300,100,100,250,300,300,200,400,400};
    unsigned i;
    setup_parts(parts,4000);
    assert(t3_psx_hurt_from_parts(parts,T3_PSX_BODY_PART_MASK,0,zones)==1);
    for(i=0;i<T3_PSX_HURT_ZONES;++i) {
        assert(zones[i].center.x==parts[map[i]].origin.x);
        assert(zones[i].radius==radius[i]);
    }
    assert(zones[8].center.y==parts[2].origin.y-120);
    assert(zones[11].center.y==parts[0].origin.y-60);
    memcpy(old,zones,sizeof(old));
    assert(!t3_psx_hurt_from_parts(parts,T3_PSX_BODY_PART_MASK&~(1u<<6),0,zones));
    assert(memcmp(old,zones,sizeof(old))==0);
    parts[6].origin.x=1000001;
    assert(!t3_psx_hurt_from_parts(parts,T3_PSX_BODY_PART_MASK,0,zones));
    assert(memcmp(old,zones,sizeof(old))==0);
    parts[6].origin.x=4030;
    assert(t3_psx_hurt_from_parts(parts,T3_PSX_BODY_PART_MASK,1,zones));
    assert(zones[0].radius==300);
    assert(zones[13].radius==600);
}
static void swept_endpoints(void) {
    t3_attack_sample_t local={0};
    t3_world_attack_t world={0};
    t3_psx_sweep_history_t history={0};
    t3_psx_sweep_history_t before;
    t3_psx_root_t current={0},prior={0};
    local.layout_kind=1;
    local.segment_count=1;
    local.segments[0].previous_start=1;
    local.segments[0].start=(t3_attack_point_t){10,0,0};
    local.segments[0].end=(t3_attack_point_t){100,0,0};
    current.cos_q12=4096;
    prior.cos_q12=4096;
    current.origin=point(1000,0,0);
    prior.origin=point(500,0,0);
    assert(t3_psx_sweep_sample(&local,42,0,&current,&prior,&history,&world)==1);
    assert(world.segments[0].start.x==510);
    assert(world.segments[0].end.x==1100);
    assert(history.valid && history.move_slot==42 && history.last_frame==0);
    current.origin=point(2500,0,0);
    local.segments[0].start.x=-500; /* intentionally irrelevant after frame 0 */
    local.segments[0].end.x=300;
    assert(t3_psx_sweep_sample(&local,42,1,&current,NULL,&history,&world)==1);
    assert(world.segments[0].start.x==1100);
    assert(world.segments[0].end.x==2800);
    assert(history.last_frame==1);
    before=history;
    assert(t3_psx_sweep_sample(&local,43,2,&current,NULL,&history,&world)==0);
    assert(memcmp(&history,&before,sizeof(before))==0);
    assert(t3_psx_sweep_sample(&local,42,4,&current,NULL,&history,&world)==0);
    assert(memcmp(&history,&before,sizeof(before))==0);
    assert(t3_psx_sweep_sample(&local,42,2,&current,NULL,&history,&world)==1);
    assert(world.segments[0].start.x==2800);
    local.segment_count=0;
    before=history;
    assert(t3_psx_sweep_sample(&local,42,3,&current,NULL,&history,&world)==-1);
    assert(memcmp(&history,&before,sizeof(before))==0);
}
static void second_segment_history(void) {
    static const unsigned layouts[3]={2u,3u,9u};
    unsigned k;
    for(k=0;k<3u;++k) {
        t3_attack_sample_t s={0};
        t3_psx_sweep_history_t hist={0};
        t3_world_attack_t world={0};
        t3_psx_root_t root={0};
        root.cos_q12=4096;
        s.segment_count=2u;
        s.layout_kind=(uint8_t)layouts[k];
        s.segments[0].start=(t3_attack_point_t){0,0,0};
        s.segments[0].end=(t3_attack_point_t){10,0,0};
        s.segments[1].start=(t3_attack_point_t){0,0,0};
        s.segments[1].end=(t3_attack_point_t){0,0,20};
        s.segments[1].previous_start=1;
        assert(t3_psx_sweep_sample(&s,99,0,&root,&root,&hist,&world)==1);
        assert(world.segments[1].end.z==20);
        root.origin.z=1000;
        s.segments[1].start.z=-400;
        s.segments[1].end.z=30;
        assert(t3_psx_sweep_sample(&s,99,1,&root,NULL,&hist,&world)==1);
        assert(world.segments[1].start.z==20);
        assert(world.segments[1].end.z==1030);
    }
}
static void complete_contact(void) {
    t3_psx_joint_t defender[T3_PSX_DRAW_PARTS];
    t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES];
    t3_world_attack_t attack={0};
    t3_psx_contact_result_t contact={0};
    setup_parts(defender,4000);
    assert(t3_psx_hurt_from_parts(defender,T3_PSX_BODY_PART_MASK,0,zones));
    attack.segment_count=1;
    attack.segments[0].start=point(3500,170,100);
    attack.segments[0].end=point(4200,170,100);
    assert(t3_psx_contact_probe(&attack,zones,&contact));
    assert(contact.evaluated==1);
    assert(contact.zone==0);
    assert(contact.segment==0);
    attack.segments[0].start=point(-9000,0,0);
    attack.segments[0].end=point(-8000,0,0);
    assert(t3_psx_contact_probe(&attack,zones,&contact));
    assert(contact.zone==-1 && contact.evaluated==1);
    contact.evaluated=7;
    attack.segment_count=3;
    assert(!t3_psx_contact_probe(&attack,zones,&contact));
    assert(contact.evaluated==7);
}
int main(void) {
    zones_from_pose();
    swept_endpoints();
    second_segment_history();
    complete_contact();
    puts("PS1 authored sweep history and pose-derived hurt zones: passed");
    return 0;
}
