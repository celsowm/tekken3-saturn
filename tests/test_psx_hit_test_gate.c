#include "psx_hit_test_gate.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static t3_psx_hit_test_facts_t usual(void) {
    t3_psx_hit_test_facts_t f={0};
    f.geometry.status=T3_SHADOW_CONTACT;
    f.geometry.zone=8;
    f.geometry.segment=1;
    f.attack_word=0x217u; /* mid, hits standing posture */
    f.defender_state=2u;
    f.attacker_active=1u;f.defender_active=1u;
    f.free_slots_mask=3u;
    f.attacker_freeze=9u;
    f.attacker_damage=40;
    return f;
}
static void contact_posture_slot_and_freeze(void) {
    t3_psx_hit_test_facts_t f=usual();
    t3_psx_hit_test_gate_t r;
    assert(t3_psx_hit_test_gate(&f,&r));
    assert(r.status==T3_HIT_GATE_ACCEPT);
    assert(r.hit_slot==1 && r.zone==8 && r.segment==1);
    assert(r.hit_cooldown==4 && r.hit_freeze_in==9);
    assert(r.set_attacker_hit_done && r.set_attacker_contact &&
           r.set_defender_got_hit);
    f.free_slots_mask=1u;
    assert(t3_psx_hit_test_gate(&f,&r));
    assert(r.hit_slot==0);
    f.free_slots_mask=2u;
    assert(t3_psx_hit_test_gate(&f,&r));
    assert(r.hit_slot==1);
    f.attacker_move_slot=0x898u;
    assert(t3_psx_hit_test_gate(&f,&r));
    assert(r.hit_freeze_in==30u);
    f.attacker_damage=0;
    assert(t3_psx_hit_test_gate(&f,&r));
    assert(r.hit_cooldown==0u);
    f.defender_in_air=1u;
    assert(t3_psx_hit_test_gate(&f,&r));
    assert(r.hit_cooldown==4u);
}
static void blockers(void) {
    t3_psx_hit_test_facts_t f=usual();
    t3_psx_hit_test_gate_t r;
    f.free_slots_mask=0u;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_NO_SLOT);
    f=usual();f.attacker_invulnerable=1;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_INVULNERABLE);
    f=usual();f.defender_invulnerable=1;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_INVULNERABLE);
    f=usual();f.attacker_active=0;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_INACTIVE);
    f=usual();f.defender_active=0;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_INACTIVE);
    f=usual();f.hit_done=1;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_ALREADY_HIT);
    f=usual();f.defender_hit_cooldown=3;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_COOLDOWN);
    f=usual();f.defender_in_throw=1;f.game_mode=7;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_THROW_LOCK);
    f.game_mode=8;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_THROW_LOCK);
    f.game_mode=0;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_ACCEPT);
    f=usual();f.attack_word=0x2u;f.defender_state=1u;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_POSTURE);
    f.defender_step_kind=2u; /* Tekken evade lane adds crouch bit */
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_ACCEPT);
    f=usual();f.geometry.status=T3_SHADOW_MISSING;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_UNAVAILABLE && r.zone==-1);
    f.geometry.status=T3_SHADOW_MISS;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_MISS);
    f.geometry.status=T3_SHADOW_INACTIVE;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_NO_ATTACK);
}
static void jump_forced_and_whiff(void) {
    t3_psx_hit_test_facts_t f=usual();
    t3_psx_hit_test_gate_t r;
    f.attack_word=0x10Fu;
    f.defender_state_class=12u;
    f.defender_state=1u;
    f.defender_air_start=4u;f.defender_air_end=20u;
    f.defender_pose_frame=5u;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_AIR_EVADE);
    f.defender_pose_frame=16u;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_ACCEPT);
    f.defender_in_air=1u;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_AIR_EVADE);
    f=usual();f.attacker_forced_hit=1;f.defender_is_cpu=1;
    f.forced_contact_flag=1;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_FORCED_UNSUPPORTED);
    f.forced_contact_flag=0;
    assert(t3_psx_hit_test_gate(&f,&r) &&
           r.status==T3_HIT_GATE_ACCEPT);
    assert(t3_psx_hit_whiff(10u,10u,0)==1);
    assert(t3_psx_hit_whiff(10u,10u,1)==0);
    assert(t3_psx_hit_whiff(9u,10u,0)==0);
    assert(t3_psx_hit_whiff(1u,256u,0)==-1);
}
static void pair_is_atomic(void) {
    t3_psx_hit_test_facts_t f[2]={usual(),usual()};
    t3_psx_hit_test_gate_t r[2],saved[2];
    f[1].geometry.zone=4;
    assert(t3_psx_hit_test_pair(f,r));
    assert(r[0].status==T3_HIT_GATE_ACCEPT &&
           r[1].status==T3_HIT_GATE_ACCEPT);
    assert(r[0].hit_slot==1 && r[1].hit_slot==1);
    memcpy(saved,r,sizeof(saved));
    f[1].geometry.zone=14;
    assert(!t3_psx_hit_test_pair(f,r));
    assert(!memcmp(saved,r,sizeof(r)));
    assert(!t3_psx_hit_test_pair(NULL,r));
    assert(!t3_psx_hit_test_pair(f,NULL));
}
int main(void) {
    contact_posture_slot_and_freeze();
    blockers();
    jump_forced_and_whiff();
    pair_is_atomic();
    puts("PS1 HitTest eligibility/slot/posture/cooldown/jump/whiff: passed");
    return 0;
}
