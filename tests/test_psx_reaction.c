#include "psx_reaction.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t records[2*T3_PSX_REACTION_BYTES];
static uint8_t throws[10];
static t3_psx_reaction_tables_t table(void) {
    t3_psx_reaction_tables_t t={records,sizeof(records),throws,sizeof(throws),NULL};
    return t;
}
static void word(uint8_t* p,unsigned at,int value) {
    uint16_t n=(uint16_t)value;
    p[at]=(uint8_t)n;
    p[at+1u]=(uint8_t)(n>>8);
}
static void setup(void) {
    static const int targets[14]={
        100,101,102,103,104,105,106,107,
        108,109,110,111,112,113
    };
    unsigned i;
    uint8_t* p=records+T3_PSX_REACTION_BYTES;
    memset(records,0,sizeof(records));memset(throws,0,sizeof(throws));
    for(i=0;i<14u;++i)word(p,i*2u,targets[i]);
    word(p,0x1Cu,-30);
    word(p,0x1Eu,5);
    word(p,0x22u,7);
    word(p,0x24u,-70);
    word(p,0x26u,9);
    word(p,0x28u,11);
    word(throws,6u,0x0C66); /* throw victim slot 3 */
}
static void directions_and_postures(void) {
    t3_psx_reaction_tables_t t=table();
    t3_psx_hit_candidate_t hit={0};
    t3_psx_reaction_facts_t facts={0};
    t3_psx_reaction_pick_t r;
    static const uint16_t stand[4]={100,104,108,106};
    static const uint16_t crouch[4]={101,105,109,107};
    static const uint16_t kind[4]={0x2A,0x27,0x2B,0x28};
    hit.zone=4u;hit.hit=1u;hit.reaction_word=1u;
    hit.reaction_kind=T3_HIT_REACTION_RECORD;
    for(unsigned quadrant=0;quadrant<4u;++quadrant) {
        facts.facing_quadrant=(uint8_t)quadrant;
        facts.defender_state=2u;
        assert(t3_psx_reaction_select(&hit,&facts,&t,&r));
        assert(r.selected && r.move_slot==stand[quadrant] &&
               r.transition==kind[quadrant]);
        assert(r.push_angle==-30 && r.push_index==5);
        facts.defender_state=1u;
        assert(t3_psx_reaction_select(&hit,&facts,&t,&r));
        assert(r.move_slot==crouch[quadrant]);
    }
}
static void guard_counter_down_and_throw(void) {
    t3_psx_reaction_tables_t t=table();
    t3_psx_hit_candidate_t hit={0};
    t3_psx_reaction_facts_t facts={0};
    t3_psx_reaction_pick_t r;
    hit.zone=8u;hit.reaction_word=1u;
    hit.guarded=1u;
    facts.defender_state=2u;
    assert(t3_psx_reaction_select(&hit,&facts,&t,&r));
    assert(r.selected && r.move_slot==110 && r.transition==0x23 &&
           r.push_index==11 && r.push_angle==-30);
    facts.defender_state=1u;
    assert(t3_psx_reaction_select(&hit,&facts,&t,&r));
    assert(r.move_slot==111);
    hit.guarded=0u;hit.hit=1u;hit.counter=1u;
    assert(t3_psx_reaction_select(&hit,&facts,&t,&r));
    assert(r.move_slot==103 && r.transition==0x26 &&
           r.push_angle==-30 && r.push_index==7);
    hit.counter=0u;facts.defender_state=4u;
    assert(t3_psx_reaction_select(&hit,&facts,&t,&r));
    assert(r.move_slot==112 && r.transition==0x25 &&
           r.push_angle==-70 && r.push_index==9);
    facts.defender_state=0x200u;
    assert(t3_psx_reaction_select(&hit,&facts,&t,&r));
    assert(r.move_slot==113);
    hit.reaction_kind=T3_HIT_REACTION_THROW_SLOT;
    hit.reaction_word=0x1003u;
    facts.defender_state=2u;
    assert(t3_psx_reaction_select(&hit,&facts,&t,&r));
    assert(r.selected && r.throw_victim && r.move_slot==0x0C66u &&
           r.transition==0x22u);
}
static void unsupported_and_class_gating(void) {
    t3_psx_reaction_tables_t t=table();
    t3_psx_hit_candidate_t hit={0};
    t3_psx_reaction_facts_t facts={0};
    t3_psx_reaction_pick_t r,saved;
    hit.zone=0u;hit.hit=1u;hit.reaction_word=1u;
    facts.defender_state=2u;
    memset(&r,0x3E,sizeof(r));saved=r;
    hit.airborne=1u;
    assert(!t3_psx_reaction_select(&hit,&facts,&t,&r));
    assert(!memcmp(&r,&saved,sizeof(r)));
    {
        /* Synthetic rows: kind k has ground offset 100+k and slot 10+k. */
        uint8_t air[T3_PSX_AIR_KIND_BYTES];
        for(unsigned k=0;k<T3_PSX_AIR_KINDS;++k){air[k*4]=(uint8_t)(100+k);air[k*4+1]=0;air[k*4+2]=(uint8_t)(10+k);air[k*4+3]=0;}
        t.air_kinds=air;facts.facing_quadrant=1;facts.defender_slot=3;
        assert(t3_psx_reaction_select(&hit,&facts,&t,&r) && r.selected && r.air &&
               r.transition==0x24u && r.air_kind==4u && r.move_slot==14u && r.ground_offset==104);
        facts.hit_dir_y=0x6E;
        assert(t3_psx_reaction_select(&hit,&facts,&t,&r) && r.air_kind==1u && r.move_slot==11u);
        facts.defender_slot=0xD62u;
        assert(t3_psx_reaction_select(&hit,&facts,&t,&r) && r.air_kind==2u);
        facts.defender_slot=0xD63u;facts.defender_pose_frame=0x13;facts.defender_root_y=-0x400;
        assert(t3_psx_air_reaction_kind(&facts)==0u);
        facts=(t3_psx_reaction_facts_t){0};facts.defender_state=2u;
        t.air_kinds=NULL;memset(&r,0x3E,sizeof(r));
    }
    hit.airborne=0;facts.forced_hit=1;
    assert(!t3_psx_reaction_select(&hit,&facts,&t,&r));
    facts.forced_hit=0;facts.facing_quadrant=4;
    assert(!t3_psx_reaction_select(&hit,&facts,&t,&r));
    facts.facing_quadrant=0;
    hit.reaction_word=50;
    assert(!t3_psx_reaction_select(&hit,&facts,&t,&r));
    assert(!memcmp(&r,&saved,sizeof(r)));
    hit.reaction_word=1;
    hit.attack_class=3u;
    assert(t3_psx_reaction_select(&hit,&facts,&t,&r));
    assert(!r.selected && r.move_slot==0);
    hit.attack_class=2u;facts.defender_state=4;
    assert(t3_psx_reaction_select(&hit,&facts,&t,&r));
    assert(!r.selected);
    facts.defender_state=2;
    assert(t3_psx_reaction_select(&hit,&facts,&t,&r));
    assert(r.selected && r.move_slot==100);
    t.records_size=1u;
    assert(!t3_psx_reaction_select(&hit,&facts,&t,&r));
}
static void sparse_records(void) {
    uint8_t sparse[88]={0};t3_psx_hit_candidate_t hit={0};
    t3_psx_reaction_facts_t facts={0};t3_psx_reaction_pick_t a,b;
    facts.defender_state=0x842;
    t3_psx_reaction_tables_t t=table();
    word(sparse,0,7);memcpy(sparse+2,records+42,42);
    word(sparse,44,9);memcpy(sparse+46,records+42,42);
    hit.reaction_word=1;hit.zone=8;hit.hit=1;
    assert(t3_psx_reaction_select(&hit,&facts,&t,&a));
    hit.reaction_word=7;
    assert(t3_psx_reaction_select_sparse(&hit,&facts,sparse,sizeof(sparse),NULL,&b));
    assert(a.move_slot==b.move_slot && a.transition==b.transition && a.push_angle==b.push_angle);
    hit.reaction_word=8;a=b;
    assert(!t3_psx_reaction_select_sparse(&hit,&facts,sparse,sizeof(sparse),NULL,&b));
    assert(!memcmp(&a,&b,sizeof(a)));
    hit.reaction_word=7;word(sparse,44,7); /* reject duplicate original indices */
    assert(!t3_psx_reaction_select_sparse(&hit,&facts,sparse,sizeof(sparse),NULL,&b));
}
int main(void) {
    setup();
    directions_and_postures();
    guard_counter_down_and_throw();
    unsupported_and_class_gating();
    sparse_records();
    puts("PS1 read-only reaction record/throw-victim selection: passed");
    return 0;
}
