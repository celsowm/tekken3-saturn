#include "psx_movevm_bridge.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* No retail bytes. Real DIVMOT sections and common-bank slot semantics,
 * synthetic move rows, reaction table and world/contact facts. */
enum {
    CHAR_MOVES=5u, MAIN_BYTES=64u+12u+CHAR_MOVES*56u+
                           T3_DIVMOT_MOVE_SLOTS*4u,
    COMMON_BYTES=64u+12u+56u
};
static uint8_t raw_main[MAIN_BYTES];
static uint8_t raw_common[COMMON_BYTES];
static uint8_t reactions[T3_PSX_REACTION_BYTES];
static t3_divmot_t main_bank,common_bank;
static t3_move_record_t moves[2];
static t3_psx_hit_facts_t damage[2];
static t3_psx_reaction_tables_t tables;

static void w16(uint8_t* p,uint16_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);
}
static void w32(uint8_t* p,uint32_t v) {
    w16(p,(uint16_t)v);w16(p+2,(uint16_t)(v>>16u));
}
static void banks(void) {
    unsigned i;
    const uint32_t move_off=76u,table_off=move_off+CHAR_MOVES*56u;
    memset(raw_main,0,sizeof(raw_main));
    memset(raw_common,0,sizeof(raw_common));
    raw_main[1]=2u;w16(raw_main+2,CHAR_MOVES);
    w32(raw_main+4,64u);w32(raw_main+8,move_off);
    w32(raw_main+12,table_off);
    for(i=3u;i<15u;++i)w32(raw_main+4u+4u*i,MAIN_BYTES);
    w16(raw_main+64u,0xC000u);
    w16(raw_main+64u+6u,3u);
    for(i=0u;i<CHAR_MOVES;++i) {
        uint8_t* p=raw_main+move_off+i*56u;
        w32(p+4u,0x842u);
        w32(p+12u,0u);
        p[24]=i==0u?30u:(i==3u?10u:20u);
    }
    for(i=0u;i<T3_DIVMOT_MOVE_SLOTS;++i)
        w32(raw_main+table_off+4u*i,T3_DIVMOT_EMPTY_SLOT);
    w32(raw_main+table_off+3u*4u,0u);
    w32(raw_main+table_off+42u*4u,1u);
    w32(raw_main+table_off+43u*4u,0x40000000u);
    w32(raw_main+table_off+4u*4u,3u);
    w32(raw_main+table_off+44u*4u,4u);
    raw_common[1]=99u;w16(raw_common+2,1u);
    w32(raw_common+4,64u);w32(raw_common+8,76u);
    w32(raw_common+12,COMMON_BYTES);
    for(i=3u;i<15u;++i)w32(raw_common+4u+4u*i,COMMON_BYTES);
    w16(raw_common+64u,0xC000u);
    w32(raw_common+76u+4u,0x842u);
    raw_common[76u+24u]=6u;
    assert(t3_divmot_open(&main_bank,raw_main,sizeof(raw_main)));
    assert(t3_divmot_open(&common_bank,raw_common,sizeof(raw_common)));
}
static void fixture(t3_psx_combat_attack_t attacks[2],
                    t3_psx_hit_apply_frame_t defenders[2],
                    t3_psx_fighter_transition_state_t before[2],
                    t3_psx_transition_facts_t timing[2],
                    t3_move_vm_t vms[2],
                    t3_psx_pose_frame_fact_t poses[2]) {
    unsigned i;
    banks();
    memset(attacks,0,sizeof(*attacks)*2u);
    memset(defenders,0,sizeof(*defenders)*2u);
    memset(before,0,sizeof(*before)*2u);
    memset(timing,0,sizeof(*timing)*2u);
    memset(vms,0,sizeof(*vms)*2u);
    memset(poses,0,sizeof(*poses)*2u);
    memset(moves,0,sizeof(moves));
    memset(damage,0,sizeof(damage));
    memset(reactions,0,sizeof(reactions));
    reactions[0u]=42u;  /* defender front */
    reactions[16u]=43u; /* defender back: common namespace */
    tables.records=reactions;
    tables.records_size=sizeof(reactions);
    tables.throw_victim_slots=NULL;
    tables.throw_slots_size=0u;
    for(i=0u;i<2u;++i) {
        moves[i].attack_word=0x217u;
        moves[i].reaction=0u;
        damage[i].attacker_damage=(int16_t)(i?60:90);
        attacks[i].test.geometry.status=T3_SHADOW_MISS;
        attacks[i].test.geometry.zone=4;
        attacks[i].test.geometry.segment=0;
        attacks[i].test.attack_word=moves[i].attack_word;
        attacks[i].test.free_slots_mask=3u;
        attacks[i].test.attacker_active=1u;
        attacks[i].test.defender_active=1u;
        attacks[i].test.defender_state=2u;
        attacks[i].test.attacker_damage=damage[i].attacker_damage;
        attacks[i].move=&moves[i];
        attacks[i].hit_facts=&damage[i];
        defenders[i].health_q16=500*65536;
        defenders[i].max_health_q16=1000*65536;
        defenders[i].reaction_tables=&tables;
        defenders[i].reaction_facts_known=1u;
        /* Player 0 is struck from behind; player 1 from the front. */
        defenders[i].reaction_facts.facing_quadrant=(uint8_t)(i?0u:2u);
        before[i].health_q16=defenders[i].health_q16;
        before[i].move_slot=3u;
        timing[i].reaction_timing_known=1u;
        timing[i].reaction_phase=T3_PSX_PHASE_HITSTUN;
        timing[i].reaction_frames=3u;
        timing[i].recovery_known=1u;
        timing[i].recovery_frames=2u;
        timing[i].recovery_move_slot=4u;
        timing[i].neutral_known=1u;
        timing[i].neutral_move_slot=3u;
        timing[i].combo_known=1u;
        timing[i].increment_combo=1u;
        timing[i].reset_combo_on_neutral=1u;
        poses[i].pose_frame_known=1u;
        assert(t3_move_vm_start(&vms[i],&main_bank,&common_bank,3u));
    }
}
static void test_slot_projection(void) {
    t3_psx_combat_attack_t a[2];
    t3_psx_hit_apply_frame_t f[2];
    t3_psx_fighter_transition_state_t s[2];
    t3_psx_transition_facts_t t[2];
    t3_move_vm_t v[2],out,old,corrupt;
    t3_psx_pose_frame_fact_t pose[2];
    fixture(a,f,s,t,v,pose);
    assert(t3_psx_movevm_project(&v[0],43u,2u,&out));
    assert(out.active_bank==&common_bank && out.slot==43u);
    assert(out.frame==2u && out.move.length==6u);
    assert(out.transitions==v[0].transitions+1u);
    memset(&out,0xCB,sizeof(out));old=out;
    assert(!t3_psx_movevm_project(&v[0],43u,7u,&out));
    assert(!memcmp(&out,&old,sizeof(out)));
    assert(!t3_psx_movevm_project(&v[0],2u,0u,&out));
    assert(!memcmp(&out,&old,sizeof(out)));
    corrupt=v[0];
    corrupt.move.attack_word++;
    assert(!t3_psx_movevm_project(&corrupt,42u,0u,&out));
    assert(!memcmp(&out,&old,sizeof(out)));
    corrupt=v[0];corrupt.transitions=UINT32_MAX;
    assert(!t3_psx_movevm_project(&corrupt,42u,0u,&out));
    assert(!memcmp(&out,&old,sizeof(out)));
}
static void test_two_player_trade(void) {
    t3_psx_combat_attack_t a[2];
    t3_psx_hit_apply_frame_t f[2];
    t3_psx_fighter_transition_state_t s[2];
    t3_psx_transition_facts_t t[2];
    t3_move_vm_t v[2];
    t3_psx_pose_frame_fact_t pose[2];
    t3_psx_movevm_bridge_preview_t r;
    fixture(a,f,s,t,v,pose);
    a[0].test.geometry.status=T3_SHADOW_CONTACT;
    a[1].test.geometry.status=T3_SHADOW_CONTACT;
    /* Explicit oracle frame; intentionally not derived from phase_frame. */
    pose[0].pose_frame=2u;
    assert(t3_psx_movevm_bridge_preview_pair(a,f,s,t,v,pose,&r));
    assert(r.transition.reaction_started[0] &&
           r.transition.reaction_started[1]);
    assert(r.transition.fighter[0].move_slot==43u);
    assert(r.transition.fighter[1].move_slot==42u);
    assert(r.vm[0].slot==43u && r.vm[0].active_bank==&common_bank);
    assert(r.vm[1].slot==42u && r.vm[1].active_bank==&main_bank);
    assert(r.vm[0].frame==2u &&
           r.transition.fighter[0].phase_frame==0u);
    assert(r.transition.fighter[0].health_q16==440*65536);
    assert(r.transition.fighter[1].health_q16==410*65536);
    assert(r.slot_changed[0] && r.slot_changed[1]);
    assert(v[0].slot==3u && v[1].slot==3u); /* no writes */
    a[0].test.geometry.status=T3_SHADOW_MISS;
    a[1].test.geometry.status=T3_SHADOW_MISS;
    s[0]=r.transition.fighter[0];
    s[1]=r.transition.fighter[1];
    f[0].health_q16=s[0].health_q16;
    f[1].health_q16=s[1].health_q16;
    v[0]=r.vm[0];v[1]=r.vm[1];
    pose[0].pose_frame=3u;pose[1].pose_frame=1u;
    assert(t3_psx_movevm_bridge_preview_pair(a,f,s,t,v,pose,&r));
    assert(r.transition.fighter[0].phase_frame==1u);
    assert(r.transition.fighter[0].frames_left==2u);
    assert(r.vm[0].frame==3u && r.vm[1].frame==1u);
    assert(!r.slot_changed[0] && !r.slot_changed[1]);
}
static void test_recovery_and_neutral(void) {
    t3_psx_combat_attack_t a[2];
    t3_psx_hit_apply_frame_t f[2];
    t3_psx_fighter_transition_state_t s[2];
    t3_psx_transition_facts_t t[2];
    t3_move_vm_t v[2];
    t3_psx_pose_frame_fact_t pose[2];
    t3_psx_movevm_bridge_preview_t r;
    fixture(a,f,s,t,v,pose);
    assert(t3_move_vm_start(&v[1],&main_bank,&common_bank,42u));
    v[1].frame=8u;
    s[1].phase=T3_PSX_PHASE_HITSTUN;
    s[1].phase_frame=8u;
    s[1].frames_left=1u;
    s[1].move_slot=42u;
    s[1].combo_hits_received=2u;
    assert(t3_psx_movevm_bridge_preview_pair(a,f,s,t,v,pose,&r));
    assert(r.transition.entered_recovery[1]);
    assert(r.vm[1].slot==4u && r.vm[1].frame==0u);
    s[1]=r.transition.fighter[1];v[1]=r.vm[1];
    pose[1].pose_frame=1u;
    assert(t3_psx_movevm_bridge_preview_pair(a,f,s,t,v,pose,&r));
    assert(r.vm[1].slot==4u && r.vm[1].frame==1u);
    s[1]=r.transition.fighter[1];v[1]=r.vm[1];
    pose[1].pose_frame=0u;
    assert(t3_psx_movevm_bridge_preview_pair(a,f,s,t,v,pose,&r));
    assert(r.transition.returned_neutral[1]);
    assert(r.vm[1].slot==3u && r.vm[1].frame==0u);
    assert(r.transition.fighter[1].combo_hits_received==0u);
}
static void test_atomic_rejection(void) {
    t3_psx_combat_attack_t a[2];
    t3_psx_hit_apply_frame_t f[2];
    t3_psx_fighter_transition_state_t s[2];
    t3_psx_transition_facts_t t[2];
    t3_move_vm_t v[2];
    t3_psx_pose_frame_fact_t pose[2];
    t3_psx_movevm_bridge_preview_t r,old;
    fixture(a,f,s,t,v,pose);
    a[0].test.geometry.status=T3_SHADOW_CONTACT;
    a[1].test.geometry.status=T3_SHADOW_CONTACT;
    memset(&r,0x71,sizeof(r));old=r;
    pose[0].pose_frame_known=0u;
    assert(!t3_psx_movevm_bridge_preview_pair(a,f,s,t,v,pose,&r));
    assert(!memcmp(&r,&old,sizeof(r)));
    pose[0].pose_frame_known=1u;
    pose[0].pose_frame=7u; /* common-bank authored move ends at 6 */
    assert(!t3_psx_movevm_bridge_preview_pair(a,f,s,t,v,pose,&r));
    assert(!memcmp(&r,&old,sizeof(r)));
    pose[0].pose_frame=0u;
    v[0].common=NULL;
    assert(!t3_psx_movevm_bridge_preview_pair(a,f,s,t,v,pose,&r));
    assert(!memcmp(&r,&old,sizeof(r)));
    v[0].common=&common_bank;
    s[0].move_slot=4u;
    assert(!t3_psx_movevm_bridge_preview_pair(a,f,s,t,v,pose,&r));
    assert(!memcmp(&r,&old,sizeof(r)));
    s[0].move_slot=3u;
    f[0].reaction_tables=NULL;
    assert(!t3_psx_movevm_bridge_preview_pair(a,f,s,t,v,pose,&r));
    assert(!memcmp(&r,&old,sizeof(r)));
    f[0].reaction_tables=&tables;
    /* KO victim slot is not yet decoded from original source. */
    f[1].health_q16=10*65536;
    s[1].health_q16=f[1].health_q16;
    assert(!t3_psx_movevm_bridge_preview_pair(a,f,s,t,v,pose,&r));
    assert(!memcmp(&r,&old,sizeof(r)));
    assert(!t3_psx_movevm_bridge_preview_pair(NULL,f,s,t,v,pose,&r));
}
int main(void) {
    test_slot_projection();
    test_two_player_trade();
    test_recovery_and_neutral();
    test_atomic_rejection();
    puts("PS1 original DIVMOT MoveVM paired reaction projection: passed");
    return 0;
}
