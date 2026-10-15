#include "psx_shadow_combat.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static t3_psx_joint_t poses[2][T3_PSX_DRAW_PARTS];
static const t3_psx_root_t identity={{0,0,0},0,4096};

static t3_attack_sample_t attack(int a,int b,unsigned layout,unsigned previous) {
    t3_attack_sample_t s={0};
    s.segment_count=1u;
    s.layout_kind=(uint8_t)layout;
    s.segments[0].start=(t3_attack_point_t){(int16_t)a,0,0};
    s.segments[0].end=(t3_attack_point_t){(int16_t)b,0,0};
    s.segments[0].previous_start=(uint8_t)previous;
    return s;
}
static void fixtures(void) {
    unsigned f,i;
    memset(poses,0,sizeof(poses));
    for(f=0;f<2u;++f)
        for(i=0;i<18u;++i)
            poses[f][i].origin=(t3_vec3i_t){10000,900,10000};
    poses[0][17].origin=(t3_vec3i_t){0,0,0};
    poses[1][17].origin=(t3_vec3i_t){0,0,0};
}
static void actors(t3_psx_shadow_actor_t a[2]) {
    unsigned i;
    memset(a,0,sizeof(t3_psx_shadow_actor_t)*2u);
    for(i=0;i<2u;++i) {
        a[i].root=identity;
        a[i].root.origin.x=(int32_t)i*500;
        a[i].pose_available=1u;
        a[i].parts=poses[i];
        a[i].parts_mask=T3_PSX_BODY_PART_MASK;
        a[i].move_slot=3u;
    }
}
static void two_sided_overlap_and_edges(void) {
    t3_psx_shadow_state_t state;
    t3_psx_shadow_result_t result[2];
    t3_psx_shadow_actor_t a[2];
    t3_attack_sample_t left=attack(0,900,0u,0u);
    t3_attack_sample_t right=attack(-900,100,0u,0u);
    fixtures();actors(a);t3_psx_shadow_reset(&state);
    a[0].attack=&left;a[1].attack=&right;
    assert(t3_psx_shadow_step(&state,a,result));
    assert(result[0].status==T3_SHADOW_CONTACT &&
           result[0].zone==0 && result[0].segment==0 &&
           result[0].new_contact==1u);
    assert(result[1].status==T3_SHADOW_CONTACT &&
           result[1].new_contact==1u);
    assert(state.contact_edges[0]==1u && state.contact_edges[1]==1u);
    assert(state.contact_frames[0]==1u && state.contact_frames[1]==1u);
    assert(t3_psx_shadow_step(&state,a,result));
    assert(result[0].status==T3_SHADOW_CONTACT && !result[0].new_contact);
    assert(state.contact_edges[0]==1u && state.contact_frames[0]==2u);

    /* A distance miss after a contact makes the next overlap a new edge. */
    a[1].root.origin.x=5000;
    assert(t3_psx_shadow_step(&state,a,result));
    assert(result[0].status==T3_SHADOW_MISS);
    a[1].root.origin.x=500;
    assert(t3_psx_shadow_step(&state,a,result));
    assert(result[0].status==T3_SHADOW_CONTACT && result[0].new_contact);
    assert(state.contact_edges[0]==2u);
}
static void invalid_pose_and_history_fail_closed(void) {
    t3_psx_shadow_state_t state;
    t3_psx_shadow_result_t result[2];
    t3_psx_shadow_actor_t a[2];
    t3_attack_sample_t strike=attack(0,900,0u,0u);
    fixtures();actors(a);t3_psx_shadow_reset(&state);
    a[0].attack=&strike;
    a[1].parts_mask^=(1u<<17);
    assert(t3_psx_shadow_step(&state,a,result));
    assert(result[0].status==T3_SHADOW_MISSING && !result[0].new_contact);
    assert(state.contact_edges[0]==0u);
    a[1].parts_mask=T3_PSX_BODY_PART_MASK;
    poses[1][17].origin.x=1000001;
    assert(t3_psx_shadow_step(&state,a,result));
    assert(result[0].status==T3_SHADOW_BAD);
    poses[1][17].origin.x=0;
    strike.layout_kind=1u;
    strike.segments[0].previous_start=1u;
    /* Pose history was not yet seeded; first swept frame is unavailable. */
    assert(t3_psx_shadow_step(&state,a,result));
    assert(result[0].status==T3_SHADOW_MISSING);
    a[0].attack=NULL;
    assert(t3_psx_shadow_step(&state,a,result));
    assert(result[0].status==T3_SHADOW_INACTIVE);
    a[0].attack=&strike;a[0].attack_frame=0u;
    assert(t3_psx_shadow_step(&state,a,result));
    assert(result[0].status==T3_SHADOW_CONTACT);
    a[0].attack_frame=1u;
    strike.segments[0].start=(t3_attack_point_t){-1200,0,0};
    strike.segments[0].end=(t3_attack_point_t){1000,0,0};
    assert(t3_psx_shadow_step(&state,a,result));
    /* Subsequent sweep MUST use previous world endpoint at x900.
     * The old-local x=-1200 would intersect the defender x=500;
     * correct world-space history x900..1000 must MISS. */
    assert(result[0].status==T3_SHADOW_MISS);
}
static void atomic_bad_call(void) {
    t3_psx_shadow_state_t state,before;
    t3_psx_shadow_result_t out[2],expected[2];
    t3_psx_shadow_actor_t a[2];
    fixtures();actors(a);t3_psx_shadow_reset(&state);
    before=state;
    memset(out,0x56,sizeof(out));memcpy(expected,out,sizeof(out));
    assert(!t3_psx_shadow_step(NULL,a,out));
    assert(!t3_psx_shadow_step(&state,NULL,out));
    assert(!t3_psx_shadow_step(&state,a,NULL));
    assert(!memcmp(&state,&before,sizeof(state)));
    assert(!memcmp(out,expected,sizeof(out)));
}
int main(void) {
    two_sided_overlap_and_edges();
    invalid_pose_and_history_fail_closed();
    atomic_bad_call();
    puts("PS1 two-fighter read-only geometric contact observer: passed");
    return 0;
}
