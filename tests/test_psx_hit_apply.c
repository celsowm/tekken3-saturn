#include "psx_hit_apply.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HEALTH(x) ((int32_t)((x)*65536))

static t3_move_record_t move;
static t3_psx_hit_facts_t fact[2];
static uint8_t reactions[2*T3_PSX_REACTION_BYTES];
static t3_psx_reaction_tables_t tables;

static void setup(void) {
    memset(&move,0,sizeof(move));
    memset(fact,0,sizeof(fact));
    memset(reactions,0,sizeof(reactions));
    move.attack_word=0x412u;
    move.reaction=1;
    fact[0].attacker_damage=40;
    fact[1].attacker_damage=40;
    reactions[T3_PSX_REACTION_BYTES]=77u; /* front stand reaction slot */
    tables.records=reactions;
    tables.records_size=sizeof(reactions);
    tables.throw_victim_slots=NULL;
    tables.throw_slots_size=0;
}
static t3_psx_hit_apply_frame_t base(void) {
    t3_psx_hit_apply_frame_t f={0};
    f.health_q16=HEALTH(100);
    f.max_health_q16=HEALTH(100);
    f.slots[0].used=1u;
    f.slots[0].zone=0u;
    f.slots[0].attacker=1u;
    f.slots[0].move=&move;
    f.slots[0].facts=&fact[0];
    f.reaction_facts_known=1u;
    f.reaction_facts.defender_state=2u;
    f.reaction_tables=&tables;
    return f;
}
static void extra_events(void) {
    t3_psx_hit_extra_t a={0},b={0};
    int16_t value=-321;
    a.damage=35;
    assert(t3_psx_hit_extra_damage(&a,&b,&value) && value==0);
    a.kind=2u;
    assert(t3_psx_hit_extra_damage(&a,&b,&value) && value==35);
    a.extra_damage=12;
    assert(t3_psx_hit_extra_damage(&a,&b,&value) && value==12);
    a.kind=3;
    assert(t3_psx_hit_extra_damage(&a,&b,&value) && value==-12);
    a.kind=0;a.extra_damage=0;a.throw_state=-1;
    b.kind=1;b.damage=80;
    assert(t3_psx_hit_extra_damage(&a,&b,&value) && value==80);
    a.kind=2;a.extra_damage=15;b.extra_damage=18;
    assert(t3_psx_hit_extra_damage(&a,&b,&value) && value==33);
    a.kind=3;
    assert(t3_psx_hit_extra_damage(&a,&b,&value) && value==3);
    b.kind=2;
    assert(t3_psx_hit_extra_damage(&a,&b,&value) && value==-15);
    a.kind=4;
    value=-321;
    assert(!t3_psx_hit_extra_damage(&a,&b,&value) && value==-321);
}
static void single_and_two_slots(void) {
    t3_psx_hit_apply_frame_t f;
    t3_psx_hit_apply_result_t r;
    setup();
    f=base();
    assert(t3_psx_hit_apply_preview(&f,&r));
    assert(r.best_slot==0 && r.last_attacker==1);
    assert(r.slots[0].damage==36 && r.slot_damage==36);
    assert(r.total_damage==36 && r.health_q16==HEALTH(64));
    assert(r.hit_clean && !r.guarded && !r.ko_started);
    assert(r.reaction_known && r.reaction.selected &&
           r.reaction.move_slot==77u && r.reaction.transition==0x2Au);

    f.slots[1]=f.slots[0];
    f.slots[1].zone=11u; /* waist 140% */
    f.slots[1].attacker=0u;
    f.defender_extra.kind=2;
    f.defender_extra.extra_damage=15;
    assert(t3_psx_hit_apply_preview(&f,&r));
    assert(r.slot_damage==92 && r.extra_damage==15 &&
           r.total_damage==107);
    assert(r.health_q16==0 && r.ko_started);
    assert(r.best_slot==1 && r.last_attacker==0);
    f.suppress_health_change=1;
    assert(t3_psx_hit_apply_preview(&f,&r));
    assert(r.health_q16==HEALTH(100) && !r.ko_started);
    assert(r.slot_damage==92 && r.total_damage==107);
    f.suppress_health_change=0;
    f.slots[1].used=0;
    f.slots[0].used=0;
    f.defender_extra.kind=0;
    assert(t3_psx_hit_apply_preview(&f,&r));
    assert(r.best_slot==-1 && r.last_attacker==-1 && r.total_damage==0 &&
           r.health_q16==HEALTH(100) && !r.reaction_known);
}
static void guard_chip_ko_and_unknown_reaction(void) {
    t3_psx_hit_apply_frame_t f;
    t3_psx_hit_apply_result_t r;
    setup();f=base();
    fact[0].attacker_damage=100;
    fact[0].defender_guard=0x10u;
    fact[0].chip_enabled=1;
    f.health_q16=HEALTH(10);
    assert(t3_psx_hit_apply_preview(&f,&r));
    assert(r.slots[0].guarded && r.slots[0].chip &&
           r.slots[0].damage==12);
    assert(r.health_q16==0 && r.ko_started);
    assert(!r.guarded && r.hit_clean); /* guard chip KO special */
    f.reaction_tables=NULL; /* absent private table -> no invented move */
    assert(t3_psx_hit_apply_preview(&f,&r));
    assert(!r.reaction_known);
    f.reaction_tables=&tables;
    f.reaction_facts_known=0;
    assert(t3_psx_hit_apply_preview(&f,&r));
    assert(!r.reaction_known);
}
static void negative_extra_heal_and_wrap(void) {
    t3_psx_hit_apply_frame_t f;
    t3_psx_hit_apply_result_t r;
    setup();f=base();
    f.slots[0].used=0;
    f.health_q16=HEALTH(20);
    f.defender_extra.kind=3;
    f.defender_extra.extra_damage=15;
    assert(t3_psx_hit_apply_preview(&f,&r));
    assert(r.extra_damage==-15 && r.health_q16==HEALTH(35));
    f.health_q16=HEALTH(95);
    assert(t3_psx_hit_apply_preview(&f,&r));
    assert(r.health_q16==HEALTH(100));
    /* Explicit MIPS 32-bit signed wrap followed by clamp, no C UB. */
    f.max_health_q16=INT32_MAX;
    f.health_q16=0x7FFE0000;
    f.defender_extra.kind=2;
    f.defender_extra.extra_damage=10;
    assert(t3_psx_hit_apply_preview(&f,&r));
    assert(r.health_q16==0x7FF40000);
}
static void invalid_inputs_atomic(void) {
    t3_psx_hit_apply_frame_t f;
    t3_psx_hit_apply_result_t r,unchanged;
    setup();f=base();
    memset(&r,0xA5,sizeof(r));unchanged=r;
    f.slots[0].zone=14;
    assert(!t3_psx_hit_apply_preview(&f,&r));
    assert(!memcmp(&r,&unchanged,sizeof(r)));
    f.slots[0].zone=0;
    f.slots[0].facts=NULL;
    assert(!t3_psx_hit_apply_preview(&f,&r));
    assert(!memcmp(&r,&unchanged,sizeof(r)));
    f.slots[0].facts=&fact[0];
    f.defender_extra.kind=255;
    assert(!t3_psx_hit_apply_preview(&f,&r));
    assert(!memcmp(&r,&unchanged,sizeof(r)));
    f.defender_extra.kind=0;
    f.slots[0].used=2;
    assert(!t3_psx_hit_apply_preview(&f,&r));
    f.slots[0].used=1;
    f.health_q16=-1;
    assert(!t3_psx_hit_apply_preview(&f,&r));
    assert(!memcmp(&r,&unchanged,sizeof(r)));
    assert(!t3_psx_hit_apply_preview(NULL,&r));
    assert(!t3_psx_hit_apply_preview(&f,NULL));
}
int main(void) {
    extra_events();
    single_and_two_slots();
    guard_chip_ko_and_unknown_reaction();
    negative_extra_heal_and_wrap();
    invalid_inputs_atomic();
    puts("PS1 HitExtraDamage and 16.16 read-only HitApply preview: passed");
    return 0;
}
