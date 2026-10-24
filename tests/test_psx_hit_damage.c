#include "psx_hit_damage.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static t3_move_record_t move(uint16_t attack,uint16_t damage) {
    t3_move_record_t m={0};
    m.attack_word=attack;
    m.damage_class=damage;
    m.reaction=39;
    return m;
}
static t3_psx_hit_facts_t facts(int16_t damage) {
    t3_psx_hit_facts_t f={0};
    f.attacker_damage=damage;
    f.defender_rel_angle=0u;
    return f;
}
static void zones_and_hits(void) {
    t3_move_record_t m=move(0x217u,100u);
    t3_psx_hit_facts_t f=facts(100);
    t3_psx_hit_candidate_t r;
    const int16_t expected[14]={90,90,90,90,100,100,120,
                                  120,130,100,100,140,125,125};
    for(unsigned i=0;i<14u;++i) {
        assert(t3_psx_hit_preview(&m,i,&f,&r));
        assert(r.damage==expected[i]);
        assert(r.zone==i && r.hit && !r.guarded &&
               !r.counter && !r.close && !r.airborne);
        assert(r.severity==5u && r.reaction_word==39u);
    }
    m.damage_class=(uint16_t)(0xC000u|100u);
    assert(t3_psx_hit_preview(&m,0u,&f,&r));
    assert(r.attack_class==3u && r.damage==90);
    /* The runtime damage may differ from the bank's original low bits,
     * due to live move events or scripted damage overrides. */
    f.attacker_damage=80;
    assert(t3_psx_hit_preview(&m,4u,&f,&r));
    assert(r.base_damage==80 && r.damage==80);
}
static void guard_chip_close_and_reactions(void) {
    t3_move_record_t m=move(0x217u,100u);
    t3_psx_hit_facts_t f=facts(100);
    t3_psx_hit_candidate_t r;
    f.defender_guard=0x10u;
    assert(t3_psx_hit_preview(&m,8u,&f,&r));
    assert(r.guarded && !r.hit && !r.chip && r.damage==0 && r.severity==2u);
    f.chip_enabled=1u;
    assert(t3_psx_hit_preview(&m,8u,&f,&r));
    assert(r.guarded && r.chip && r.damage==12 && r.severity==5u);
    f.defender_rel_angle=0x4000u;
    assert(t3_psx_hit_preview(&m,8u,&f,&r));
    assert(!r.guarded && r.hit && r.damage==130);
    f.defender_rel_angle=0u;
    f.defender_guard=0u;
    m.close_reaction=5u;
    f.close_table_available=1u;
    f.close_threshold=800u;
    f.fighter_distance=799u;
    f.close_reaction_word=0x1009u;
    assert(t3_psx_hit_preview(&m,8u,&f,&r));
    assert(r.close && r.damage==150 &&
           r.reaction_word==0x1009u &&
           r.reaction_kind==T3_HIT_REACTION_THROW_SLOT &&
           r.severity==9u);
    f.fighter_distance=800u; /* strict < threshold */
    assert(t3_psx_hit_preview(&m,8u,&f,&r));
    assert(!r.close && r.damage==130 && r.reaction_word==39);
}
static void counter_and_juggle(void) {
    t3_move_record_t m=move(0x217u,100u);
    t3_psx_hit_facts_t f=facts(100);
    t3_psx_hit_candidate_t r;
    f.defender_damage=80;
    f.defender_active_first=10;
    f.defender_pose_frame=5;
    assert(t3_psx_hit_preview(&m,4u,&f,&r));
    assert(r.counter && r.damage==123 && r.severity==11u);
    f.attacker_power=1u;
    assert(t3_psx_hit_preview(&m,4u,&f,&r));
    assert(r.counter && r.damage==144);
    f.attacker_power=0u;
    f.defender_damage=0;
    f.defender_in_air=1u;
    assert(t3_psx_hit_preview(&m,8u,&f,&r));
    assert(r.airborne && r.damage==104 && r.severity==12u);
    f.juggle_count=2u;
    assert(t3_psx_hit_preview(&m,8u,&f,&r));
    assert(r.damage==65);
    f.defender_in_air=0u;
    f.defender_state=0x400u;
    assert(t3_psx_hit_preview(&m,4u,&f,&r));
    assert(r.airborne && r.damage==50);
    f.defender_state=0u;
    f.attacker_damage=0;
    assert(t3_psx_hit_preview(&m,4u,&f,&r));
    assert(r.no_damage && r.damage==0 && r.severity==1u &&
           !r.guarded && !r.chip && !r.close && !r.hit && !r.counter &&
           !r.airborne);
    f.defender_guard=0x10u;
    f.chip_enabled=1u;
    m.close_reaction=15u;
    f.close_table_available=0u;
    assert(t3_psx_hit_preview(&m,4u,&f,&r));
    assert(r.no_damage && r.damage==0 && r.severity==1u &&
           !r.guarded && !r.chip && !r.close);
}
static void strict_and_best(void) {
    t3_move_record_t m=move(0x217u,100u);
    t3_psx_hit_facts_t f=facts(100);
    t3_psx_hit_candidate_t out,saved,a,b;
    memset(&out,0x75,sizeof(out));saved=out;
    assert(!t3_psx_hit_preview(NULL,3u,&f,&out));
    assert(!t3_psx_hit_preview(&m,14u,&f,&out));
    assert(!t3_psx_hit_preview(&m,0u,NULL,&out));
    assert(!t3_psx_hit_preview(&m,0u,&f,NULL));
    assert(!memcmp(&out,&saved,sizeof(out)));
    m.close_reaction=2u;
    assert(!t3_psx_hit_preview(&m,4u,&f,&out));
    f.close_table_available=1u;
    /* A zero distance threshold is never close (unsigned distance < 0). */
    assert(t3_psx_hit_preview(&m,4u,&f,&a) && !a.close);
    f.close_threshold=400u;
    f.defender_guard=0x200u; /* non-guard bits are not a guard mask */
    assert(!t3_psx_hit_preview(&m,4u,&f,&out));
    assert(!memcmp(&out,&saved,sizeof(out)));
    f.defender_guard=0;
    f.close_threshold=400u;
    f.fighter_distance=1000u;
    assert(t3_psx_hit_preview(&m,4u,&f,&a));
    f.fighter_distance=300u;
    assert(t3_psx_hit_preview(&m,4u,&f,&b));
    assert(t3_psx_hit_best(&a,&b)==1);
    assert(t3_psx_hit_best(&b,&a)==0);
    a.severity=5;b.severity=5;
    a.damage=100;b.damage=101;
    assert(t3_psx_hit_best(&a,&b)==1);
    b.damage=100;
    assert(t3_psx_hit_best(&a,&b)==0);
    assert(t3_psx_hit_best(NULL,&b)==-1);
}
int main(void) {
    zones_and_hits();
    guard_chip_close_and_reactions();
    counter_and_juggle();
    strict_and_best();
    puts("PS1 read-only HitClassify/HitDamage preview: passed");
    return 0;
}
