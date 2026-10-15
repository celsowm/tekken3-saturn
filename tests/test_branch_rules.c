/* Direct checks of independently implemented branch predicates. No PS1 files. */
#include "branch_rules.h"
#include "input_history.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

static void test_restrictions(void) {
    t3_branch_context_t f = {0};
    assert(t3_branch_restriction_matches(0,2,NULL));
    assert(t3_branch_restriction_matches(3,2,NULL));
    assert(!t3_branch_restriction_matches(4,2,NULL));
    assert(!t3_branch_restriction_matches(0x1A,2,NULL));
    assert(t3_branch_restriction_matches(0x1B,2,NULL));
    assert(!t3_branch_restriction_matches(0x33,2,NULL));
    f.known=T3_FACT_IDENTITY;
    f.opponent_bank=4;f.own_character=7;
    assert(t3_branch_restriction_matches(0x33,2,&f));
    assert(!t3_branch_restriction_matches(0x34,2,&f));
    assert(t3_branch_restriction_matches(0x4D,2,&f));
    assert(!t3_branch_restriction_matches(0x4E,2,&f));
    assert(!t3_branch_restriction_matches(0x64,2,&f));
    assert(t3_branch_restriction_matches(0x65,2,&f));
}

static void test_conditions(void) {
    t3_branch_context_t f={0};
    unsigned type;
    assert(t3_branch_condition_matches(0,0,NULL));
    for(type=1;type<0x45;++type)
        assert(!t3_branch_condition_matches((uint8_t)type,999,NULL));
    f.known=T3_FACT_CONTACT;
    f.contact=1;f.guarded=0;f.whiffed=1;
    assert(t3_branch_condition_matches(1,0,&f));
    assert(t3_branch_condition_matches(0x1A,0,&f));
    assert(!t3_branch_condition_matches(0x1B,0,&f));
    assert(t3_branch_condition_matches(0x1C,0,&f));
    f.guarded=1;
    assert(!t3_branch_condition_matches(0x1A,0,&f));
    assert(t3_branch_condition_matches(0x1B,0,&f));

    f.known=T3_FACT_DISTANCE;
    f.distance=500;f.adjusted_distance=190;
    assert(!t3_branch_condition_matches(0x18,499,&f));
    assert(t3_branch_condition_matches(0x18,500,&f));
    assert(t3_branch_condition_matches(0x19,500,&f));
    assert(!t3_branch_condition_matches(0x19,501,&f));
    assert(!t3_branch_condition_matches(2,200,&f));
    f.known|=T3_FACT_THROW;f.throw_situation=2;
    assert(t3_branch_condition_matches(2,190,&f));
    assert(t3_branch_condition_matches(0x14,190,&f));
    assert(!t3_branch_condition_matches(0x15,190,&f));
    f.throw_situation=6;
    assert(t3_branch_condition_matches(0x14,190,&f));
    assert(!t3_branch_condition_matches(2,190,&f));
    assert(!t3_branch_condition_matches(0x14,189,&f));

    f.known=T3_FACT_OPPONENT;
    f.opponent_state=2;f.opponent_damage=40;f.opponent_attack=0x412;
    f.opponent_frame=4;f.opponent_active_last=9;
    f.opponent_transition=0;
    assert(t3_branch_condition_matches(0x1D,0,&f));
    assert(t3_branch_condition_matches(0x1E,0,&f));
    assert(t3_branch_condition_matches(0x1F,0,&f));
    assert(!t3_branch_condition_matches(0x20,0,&f));
    assert(!t3_branch_condition_matches(0x21,0,&f));
    assert(t3_branch_condition_matches(0x22,0,&f));
    assert(!t3_branch_condition_matches(0x23,0,&f));
    assert(!t3_branch_condition_matches(0x30,0,&f));
    f.known|=T3_FACT_DISTANCE;f.distance=0x600;
    assert(t3_branch_condition_matches(0x30,0,&f));
    f.opponent_frame=10;
    assert(!t3_branch_condition_matches(0x30,0,&f));
    f.opponent_state=4;f.opponent_frame=0;f.opponent_damage=0;
    assert(t3_branch_condition_matches(0x35,0,&f));
    assert(t3_branch_condition_matches(0x21,0,&f));
    assert(!t3_branch_condition_matches(0x36,0,&f));
    assert(t3_branch_condition_matches(0x37,0x600,&f));
    f.opponent_air_start=2;
    assert(!t3_branch_condition_matches(0x37,0x600,&f));

    f.known=T3_FACT_ORIENTATION;
    f.relative_angle=0x5000;f.heading_delta=0x6000;
    f.facing_quadrant=3;f.opponent_quadrant=2;
    assert(t3_branch_condition_matches(0x24,0,&f));
    assert(t3_branch_condition_matches(0x25,0,&f));
    assert(!t3_branch_condition_matches(0x26,0,&f));
    assert(t3_branch_condition_matches(0x29,0,&f));
    assert(t3_branch_condition_matches(0x2E,0,&f));
    assert(!t3_branch_condition_matches(0x2F,0,&f));
    f.known|=T3_FACT_SIDE;
    f.side_flag=1;
    assert(t3_branch_condition_matches(0x3F,0,&f));
    assert(t3_branch_condition_matches(0x41,0,&f));
    assert(!t3_branch_condition_matches(0x40,0,&f));

    f.known=T3_FACT_RECOVERY;
    assert(t3_branch_condition_matches(0x31,0,&f));
    f.recover_frames=1;
    assert(t3_branch_condition_matches(0x32,0,&f));
    f.known=T3_FACT_COLLISION;
    f.body_contact=1;
    assert(t3_branch_condition_matches(0x33,0,&f));
    assert(!t3_branch_condition_matches(0x34,0,&f));

    f.known=T3_FACT_BUTTON_TAPS;f.tap_lp=1;f.tap_rp=0;f.tap_both=1;
    assert(t3_branch_condition_matches(0x3A,0,&f));
    assert(!t3_branch_condition_matches(0x3B,0,&f));
    assert(t3_branch_condition_matches(0x3C,0,&f));
    f.known=T3_FACT_HEALTH|T3_FACT_LAUNCH;
    f.own_health=100;f.launch_armed=0;
    assert(t3_branch_condition_matches(0x42,0,&f));
    f.own_health=0;
    assert(t3_branch_condition_matches(0x3D,0,&f));
    assert(!t3_branch_condition_matches(0x42,0,&f));
    f.known=T3_FACT_POWER;f.own_power_timer=2;
    assert(t3_branch_condition_matches(0x43,0,&f));
    f.own_power_timer=0;
    assert(t3_branch_condition_matches(0x44,0,&f));
    assert(!t3_branch_condition_matches(0x45,0,&f));
}

static void test_sequence_registry(void) {
    t3_input_history_t h;
    t3_command_sequence_t seq={0};
    t3_history_reset(&h);
    t3_history_push(&h,(t3_input_t){T3_IN_RIGHT,0});
    t3_history_push(&h,(t3_input_t){T3_IN_RIGHT|T3_IN_FORWARD,0});
    t3_history_push(&h,(t3_input_t){T3_IN_FORWARD,T3_IN_LP});
    seq.command=0xC015;
    seq.window=25;
    seq.steps[0]=2;seq.steps[1]=3;seq.steps[2]=0x106;
    assert(!t3_command_matches_table(0xC015,&h,NULL,0));
    assert(t3_command_matches_table(0xC015,&h,&seq,1));
    assert(!t3_command_matches_table(0xC016,&h,&seq,1));
    assert(!t3_command_matches_table(0xC800,&h,&seq,1));
    seq.window=2;
    assert(!t3_command_matches_table(0xC015,&h,&seq,1));
    seq.window=25;
    /* Corrupted unterminated sequences must fail safely. */
    for(unsigned i=0;i<T3_INPUT_SEQUENCE_MAX+1u;++i)
        seq.steps[i]=6;
    assert(!t3_command_matches_table(0xC015,&h,&seq,1));
}

int main(void) {
    test_restrictions();
    test_conditions();
    test_sequence_registry();
    puts("PS1 branch predicates and sequence registry: passed");
    return 0;
}
