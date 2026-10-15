#include "psx_state_transition.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static t3_move_record_t move[2];
static t3_psx_hit_facts_t damage[2];
static uint8_t records[T3_PSX_REACTION_BYTES];
static t3_psx_reaction_tables_t table;
static void fixture(t3_psx_combat_attack_t a[2],
                    t3_psx_hit_apply_frame_t f[2],
                    t3_psx_fighter_transition_state_t s[2],
                    t3_psx_transition_facts_t timing[2]) {
    unsigned i;
    memset(a,0,sizeof(*a)*2u);
    memset(f,0,sizeof(*f)*2u);
    memset(s,0,sizeof(*s)*2u);
    memset(timing,0,sizeof(*timing)*2u);
    memset(move,0,sizeof(move));
    memset(damage,0,sizeof(damage));
    memset(records,0,sizeof(records));
    /* Synthetic 42-byte record; exact original bytes remain private. */
    records[0]=42u;
    records[8]=43u;
    table.records=records;
    table.records_size=sizeof(records);
    table.throw_victim_slots=NULL;
    table.throw_slots_size=0u;
    for(i=0u;i<2u;++i) {
        move[i].attack_word=0x217u;
        move[i].reaction=0u;
        damage[i].attacker_damage=(int16_t)(i?60:90);
        a[i].test.geometry.status=T3_SHADOW_MISS;
        a[i].test.geometry.zone=4;
        a[i].test.geometry.segment=0;
        a[i].test.attack_word=move[i].attack_word;
        a[i].test.free_slots_mask=3u;
        a[i].test.attacker_active=1u;
        a[i].test.defender_active=1u;
        a[i].test.defender_state=2u;
        a[i].test.attacker_damage=damage[i].attacker_damage;
        a[i].move=&move[i];
        a[i].hit_facts=&damage[i];
        f[i].health_q16=500*65536;
        f[i].max_health_q16=1000*65536;
        f[i].reaction_facts_known=1u;
        f[i].reaction_tables=&table;
        s[i].health_q16=f[i].health_q16;
        s[i].move_slot=3u;
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
    }
}
static void trades_and_expiry(void) {
    t3_psx_combat_attack_t a[2];
    t3_psx_hit_apply_frame_t f[2];
    t3_psx_fighter_transition_state_t s[2];
    t3_psx_transition_facts_t facts[2];
    t3_psx_transition_preview_t r;
    unsigned i;
    fixture(a,f,s,facts);
    a[0].test.geometry.status=T3_SHADOW_CONTACT;
    a[1].test.geometry.status=T3_SHADOW_CONTACT;
    assert(t3_psx_combat_transition_preview_pair(a,f,s,facts,&r));
    assert(r.reaction_started[0] && r.reaction_started[1]);
    assert(r.fighter[0].phase==T3_PSX_PHASE_HITSTUN);
    assert(r.fighter[1].phase==T3_PSX_PHASE_HITSTUN);
    assert(r.fighter[0].combo_hits_received==1u &&
           r.fighter[1].combo_hits_received==1u);
    assert(r.fighter[0].health_q16==440*65536);
    assert(r.fighter[1].health_q16==410*65536);
    assert(s[0].phase==T3_PSX_PHASE_NEUTRAL);
    for(i=0u;i<2u;++i)a[i].test.geometry.status=T3_SHADOW_MISS;
    s[0]=r.fighter[0];s[1]=r.fighter[1];
    f[0].health_q16=s[0].health_q16;
    f[1].health_q16=s[1].health_q16;
    for(i=0u;i<2u;++i) {
        assert(t3_psx_combat_transition_preview_pair(a,f,s,facts,&r));
        assert(r.fighter[0].phase==T3_PSX_PHASE_HITSTUN);
        assert(r.fighter[0].frames_left==(uint16_t)(2u-i));
        s[0]=r.fighter[0];s[1]=r.fighter[1];
    }
    assert(t3_psx_combat_transition_preview_pair(a,f,s,facts,&r));
    assert(r.entered_recovery[0] && r.entered_recovery[1]);
    assert(r.fighter[0].phase==T3_PSX_PHASE_RECOVERY);
    assert(r.fighter[0].move_slot==4u);
    s[0]=r.fighter[0];s[1]=r.fighter[1];
    assert(t3_psx_combat_transition_preview_pair(a,f,s,facts,&r));
    assert(r.fighter[0].frames_left==1u);
    s[0]=r.fighter[0];s[1]=r.fighter[1];
    assert(t3_psx_combat_transition_preview_pair(a,f,s,facts,&r));
    assert(r.returned_neutral[0] && r.returned_neutral[1]);
    assert(r.fighter[0].phase==T3_PSX_PHASE_NEUTRAL);
    assert(r.fighter[0].combo_hits_received==0u);
}
static void knockdown_and_fail_closed(void) {
    t3_psx_combat_attack_t a[2];
    t3_psx_hit_apply_frame_t f[2];
    t3_psx_fighter_transition_state_t s[2];
    t3_psx_transition_facts_t facts[2];
    t3_psx_transition_preview_t r,unchanged;
    fixture(a,f,s,facts);
    a[0].test.geometry.status=T3_SHADOW_CONTACT;
    facts[1].reaction_phase=T3_PSX_PHASE_KNOCKDOWN;
    assert(t3_psx_combat_transition_preview_pair(a,f,s,facts,&r));
    assert(r.fighter[1].phase==T3_PSX_PHASE_KNOCKDOWN);
    memset(&r,0xA5,sizeof(r));unchanged=r;
    facts[1].reaction_timing_known=0u;
    assert(!t3_psx_combat_transition_preview_pair(a,f,s,facts,&r));
    assert(!memcmp(&r,&unchanged,sizeof(r)));
    facts[1].reaction_timing_known=1u;
    f[1].reaction_tables=NULL;
    assert(!t3_psx_combat_transition_preview_pair(a,f,s,facts,&r));
    assert(!memcmp(&r,&unchanged,sizeof(r)));
    f[1].reaction_tables=&table;
    f[1].health_q16--;
    assert(!t3_psx_combat_transition_preview_pair(a,f,s,facts,&r));
    assert(!memcmp(&r,&unchanged,sizeof(r)));
    assert(!t3_psx_combat_transition_preview_pair(NULL,f,s,facts,&r));
}
static void expiry_requires_original_destinations(void) {
    t3_psx_combat_attack_t a[2];
    t3_psx_hit_apply_frame_t f[2];
    t3_psx_fighter_transition_state_t s[2];
    t3_psx_transition_facts_t facts[2];
    t3_psx_transition_preview_t r,old;
    fixture(a,f,s,facts);
    s[1].phase=T3_PSX_PHASE_BLOCKSTUN;
    s[1].frames_left=1u;
    facts[1].recovery_known=0u;
    memset(&r,0x5A,sizeof(r));old=r;
    assert(!t3_psx_combat_transition_preview_pair(a,f,s,facts,&r));
    assert(!memcmp(&r,&old,sizeof(r)));
    facts[1].recovery_known=1u;
    assert(t3_psx_combat_transition_preview_pair(a,f,s,facts,&r));
    assert(r.fighter[1].phase==T3_PSX_PHASE_RECOVERY);
}
int main(void) {
    trades_and_expiry();
    knockdown_and_fail_closed();
    expiry_requires_original_destinations();
    puts("PS1 read-only hitstun/knockdown/recovery transition harness: passed");
    return 0;
}
