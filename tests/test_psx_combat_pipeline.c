#include "psx_combat_pipeline.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static t3_move_record_t moves[2];
static t3_psx_hit_facts_t damage[2];

static void setup(t3_psx_combat_attack_t attacks[2],
                  t3_psx_hit_apply_frame_t defenders[2]) {
    unsigned i;
    memset(attacks,0,sizeof(t3_psx_combat_attack_t)*2u);
    memset(defenders,0,sizeof(t3_psx_hit_apply_frame_t)*2u);
    memset(moves,0,sizeof(moves));
    memset(damage,0,sizeof(damage));
    for(i=0;i<2u;++i) {
        moves[i].attack_word=0x217u;
        moves[i].reaction=1u;
        damage[i].attacker_damage=(int16_t)(i?60:90);
        attacks[i].test.geometry.status=T3_SHADOW_CONTACT;
        attacks[i].test.geometry.zone=4;
        attacks[i].test.geometry.segment=0;
        attacks[i].test.attack_word=0x217u;
        attacks[i].test.free_slots_mask=3u;
        attacks[i].test.attacker_active=1u;
        attacks[i].test.defender_active=1u;
        attacks[i].test.defender_state=2u;
        attacks[i].test.attacker_damage=damage[i].attacker_damage;
        attacks[i].test.attacker_freeze=(uint8_t)(i?12:9);
        attacks[i].move=&moves[i];
        attacks[i].hit_facts=&damage[i];
        defenders[i].health_q16=500*65536;
        defenders[i].max_health_q16=1000*65536;
    }
}
static void trade_and_posture_filter(void) {
    t3_psx_combat_attack_t a[2];
    t3_psx_hit_apply_frame_t f[2],before[2];
    t3_psx_combat_preview_t r;
    setup(a,f);
    memcpy(before,f,sizeof(f));
    assert(t3_psx_combat_preview_pair(a,f,&r));
    assert(r.gate[0].status==T3_HIT_GATE_ACCEPT);
    assert(r.gate[1].status==T3_HIT_GATE_ACCEPT);
    assert(r.gate[0].hit_slot==1 && r.gate[1].hit_slot==1);
    /* Attacker 0 lands on defender 1; attacker 1 lands on defender 0. */
    assert(r.defend[0].health_q16==440*65536);
    assert(r.defend[1].health_q16==410*65536);
    assert(r.defend[0].best_slot==1 && r.defend[1].best_slot==1);
    assert(r.defend[0].last_attacker==1 &&
           r.defend[1].last_attacker==0);
    assert(memcmp(before,f,sizeof(f))==0);
    a[0].test.defender_state=1u;
    a[0].test.attack_word=2u; /* cannot hit crouch */
    moves[0].attack_word=2u;
    assert(t3_psx_combat_preview_pair(a,f,&r));
    assert(r.gate[0].status==T3_HIT_GATE_POSTURE);
    assert(r.defend[1].health_q16==500*65536);
    assert(r.defend[1].best_slot==-1);
    assert(r.defend[0].health_q16==440*65536);
    a[0].test.defender_step_kind=2u;
    assert(t3_psx_combat_preview_pair(a,f,&r));
    assert(r.gate[0].status==T3_HIT_GATE_ACCEPT);
    assert(r.defend[1].health_q16==410*65536);
}
static void guard_reaction_and_missing_geometry(void) {
    t3_psx_combat_attack_t a[2];
    t3_psx_hit_apply_frame_t f[2];
    t3_psx_combat_preview_t r;
    setup(a,f);
    a[0].test.geometry.status=T3_SHADOW_MISSING;
    a[1].test.geometry.status=T3_SHADOW_MISS;
    assert(t3_psx_combat_preview_pair(a,f,&r));
    assert(r.gate[0].status==T3_HIT_GATE_UNAVAILABLE);
    assert(r.gate[1].status==T3_HIT_GATE_MISS);
    assert(r.defend[0].health_q16==500*65536);
    assert(r.defend[1].health_q16==500*65536);
    assert(!r.defend[0].reaction_known && !r.defend[1].reaction_known);
    /* A guarded original attack is classified via injected guard facts,
     * NOT the fallback Saturn T3_IN_GUARD key. */
    setup(a,f);
    damage[0].defender_guard=0x10u;
    moves[0].attack_word=0x217u; /* has 0x10 guard-block bit */
    assert(t3_psx_combat_preview_pair(a,f,&r));
    assert(r.defend[1].guarded && !r.defend[1].hit_clean);
    assert(r.defend[1].health_q16==500*65536);
    assert(r.defend[0].health_q16==440*65536);
}
static void rejects_stale_hit_slots_atomically(void) {
    t3_psx_combat_attack_t a[2];
    t3_psx_hit_apply_frame_t f[2];
    t3_psx_combat_preview_t r,untouched;
    setup(a,f);
    memset(&r,0xB7,sizeof(r));untouched=r;
    f[1].slots[1].used=1u; /* contradicts free_slots_mask=3 */
    assert(!t3_psx_combat_preview_pair(a,f,&r));
    assert(memcmp(&r,&untouched,sizeof(r))==0);
    f[1].slots[1].used=0u;
    a[0].hit_facts=NULL; /* cannot invent defender power/guard state */
    assert(!t3_psx_combat_preview_pair(a,f,&r));
    assert(memcmp(&r,&untouched,sizeof(r))==0);
    assert(!t3_psx_combat_preview_pair(NULL,f,&r));
    assert(!t3_psx_combat_preview_pair(a,NULL,&r));
    assert(!t3_psx_combat_preview_pair(a,f,NULL));
}
int main(void) {
    trade_and_posture_filter();
    guard_reaction_and_missing_geometry();
    rejects_stale_hit_slots_atomically();
    puts("PS1 simultaneous geometry -> HitTest -> HitApply preview: passed");
    return 0;
}
