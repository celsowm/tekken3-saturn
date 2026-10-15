#include "divmot.h"
#include "move_vm.h"
#include "input_history.h"
#include "fight.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define N_BYTES (64u+3u*12u+2u*56u+T3_DIVMOT_MOVE_SLOTS*4u)
static uint8_t bank_bytes[N_BYTES];

static void put16(uint8_t* p, uint16_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8u);
}
static void put32(uint8_t* p, uint32_t v) {
    put16(p,(uint16_t)v);put16(p+2,(uint16_t)(v>>16u));
}
static void make_bank(void) {
    unsigned i;
    const uint32_t b0=64u, b1=b0+3u*12u, b2=b1+2u*56u;
    memset(bank_bytes,0,sizeof(bank_bytes));
    bank_bytes[1]=2; put16(bank_bytes+2,2);
    put32(bank_bytes+4,b0);put32(bank_bytes+8,b1);
    put32(bank_bytes+12,b2);
    for(i=3;i<15;++i) put32(bank_bytes+4+i*4u,N_BYTES);
    /* neutral move=0, attack move=1 */
    put32(bank_bytes+b1+4,0x842);
    put32(bank_bytes+b1+12,0);bank_bytes[b1+24]=30;
    put32(bank_bytes+b1+56+4,0x842);
    put16(bank_bytes+b1+56+8,0x412);
    put32(bank_bytes+b1+56+12,2);
    bank_bytes[b1+56+24]=10;
    bank_bytes[b1+56+45]=4;
    bank_bytes[b1+56+46]=6;
    put16(bank_bytes+b1+56+20,70);
    /* branch0: LP -> slot 1 (valid frames 0..29) */
    put16(bank_bytes+b0,1);
    put16(bank_bytes+b0+6,1);
    bank_bytes[b0+10]=29;
    /* branch1: list end, default goes to slot0 */
    put16(bank_bytes+b0+12,0xC000);
    /* branch2: attack default back to slot0 */
    put16(bank_bytes+b0+24,0xC000);
    for (i=0;i<T3_DIVMOT_MOVE_SLOTS;++i)
        put32(bank_bytes+b2+i*4u,T3_DIVMOT_EMPTY_SLOT);
    put32(bank_bytes+b2,0);
    put32(bank_bytes+b2+4,1);
}

static t3_input_t input(uint16_t held, uint16_t pressed) {
    t3_input_t out={held,pressed};return out;
}

static void test_commands(void) {
    t3_input_history_t h;
    t3_history_reset(&h);
    assert(!t3_command_matches(0xC001,&h));
    t3_history_push(&h,input(T3_IN_FORWARD,0));
    t3_history_push(&h,input(0,0));
    t3_history_push(&h,input(T3_IN_FORWARD,0));
    assert(t3_command_matches(0xC001,&h));
    assert(!t3_command_matches(0xC002,&h));
    assert(t3_command_matches((uint16_t)(1u<<10),&h)); /* forward direction */
    assert(!t3_command_matches((uint16_t)(1u<<8),&h));
    t3_history_push(&h,input(T3_IN_FORWARD,T3_IN_LP));
    assert(t3_command_matches(1,&h));
    assert(t3_command_matches(0x11,&h)); /* any newly pressed LP */
    assert(t3_command_matches(0xC00D,&h));
    assert(!t3_command_matches(0xC017,&h)); /* no fake DP matcher */
    assert(!t3_command_matches(2,&h)); /* RP not pressed */
    /* Quarter-circle: down (2), down-forward (3), forward+LP (6+LP). */
    t3_history_reset(&h);
    t3_history_push(&h,input(T3_IN_RIGHT,0));
    t3_history_push(&h,input(T3_IN_RIGHT|T3_IN_FORWARD,0));
    t3_history_push(&h,input(T3_IN_FORWARD,T3_IN_LP));
    { const uint16_t seq[]={2,3,(1u<<8)|6u,0};
      assert(t3_sequence_matches(&h,seq,25));
      assert(!t3_sequence_matches(&h,seq,2)); }
    for(unsigned i=0;i<70;++i)t3_history_push(&h,input(0,0));
    assert(h.count==T3_INPUT_HISTORY_FRAMES);
    assert(t3_history_at(&h,59).direction==5u);
    assert(t3_history_at(&h,60).direction==5u);
}

static void test_bank_and_branch(void) {
    t3_divmot_t bank={0};
    t3_move_record_t neutral={0},attack={0};
    t3_branch_record_t branch={0};
    t3_input_history_t input_hist;
    make_bank();
    assert(t3_divmot_open(&bank,bank_bytes,sizeof(bank_bytes)));
    assert(bank.branch_count==3 && bank.move_count==2);
    assert(t3_divmot_slot(&bank,NULL,0,&neutral)==1);
    assert(t3_divmot_slot(&bank,NULL,1,&attack)==1);
    assert(t3_divmot_slot(&bank,NULL,2,&attack)==0);
    assert(t3_divmot_slot(&bank,NULL,4023,&attack)==-1);
    assert(neutral.length==30 && neutral.branch_index==0);
    assert(attack.length==10 && attack.branch_index==2);
    assert(attack.attack_word==0x412 && attack.damage_class==70);
    assert(attack.active_first==4 && attack.active_last==6);
    t3_history_reset(&input_hist);
    t3_history_push(&input_hist,input(0,T3_IN_LP));
    assert(t3_divmot_branch_pick(&bank,neutral,&input_hist,5,&branch));
    assert(branch.target_slot==1 && branch.command==1);
    assert(t3_divmot_branch_pick(&bank,neutral,&input_hist,35,&branch));
    assert(branch.command==0xC000 && branch.target_slot==0);
    t3_history_reset(&input_hist);
    t3_history_push(&input_hist,input(0,0));
    assert(!t3_divmot_branch_pick(&bank,neutral,&input_hist,5,&branch));
    assert(t3_divmot_branch_pick(&bank,neutral,&input_hist,30,&branch));
    assert(branch.command==0xC000 && branch.target_slot==0);
    assert(t3_divmot_branch_pick(&bank,attack,&input_hist,10,&branch));
    assert(branch.target_slot==0);
    /* Bad section length must not be accepted. */
    bank_bytes[4]=0;
    assert(!t3_divmot_open(&bank,bank_bytes,sizeof(bank_bytes)));
}

static void test_vm_execution(void) {
    t3_divmot_t bank={0};
    t3_input_history_t h;
    t3_move_vm_t vm={0};
    unsigned i;
    make_bank();
    assert(t3_divmot_open(&bank,bank_bytes,sizeof(bank_bytes)));
    assert(t3_move_vm_start(&vm,&bank,NULL,0));
    assert(vm.slot==0 && vm.move.length==30 && vm.frame==0);
    t3_history_reset(&h);
    t3_history_push(&h,input(0,T3_IN_LP));
    assert(t3_move_vm_tick(&vm,&h)==1);
    assert(vm.slot==1 && vm.frame==0 && vm.move.attack_word==0x412);
    /* Attack runs for 10 frames, then follows its default continuation. */
    t3_history_reset(&h);
    t3_history_push(&h,input(0,0));
    for (i=0; i<10; ++i) assert(t3_move_vm_tick(&vm,&h)==0);
    assert(vm.frame==10);
    assert(t3_move_vm_tick(&vm,&h)==1);
    assert(vm.slot==0 && vm.transitions==2);
    assert(!t3_move_vm_start(&vm,&bank,NULL,4)); /* empty move slot */
}

static void test_common_bank_branches(void) {
    /* Common bank branch table has its OWN row0 terminator, whereas
     * character bank row0 is LP -> attack. VM must never confuse them. */
    enum { COMMON_BYTES=64+12+56 };
    uint8_t raw[COMMON_BYTES]={0};
    const unsigned b0=64u, b1=76u, b2=132u, b3=132u;
    t3_divmot_t bank={0},common={0};
    t3_move_vm_t vm={0};
    t3_input_history_t h;
    const t3_divmot_t* owner=NULL;
    t3_move_record_t move;
    unsigned i;
    make_bank();
    raw[1]=99;put16(raw+2,1);
    put32(raw+4,b0);put32(raw+8,b1);put32(raw+12,b2);
    for (i=3;i<15u;++i)put32(raw+4+i*4u,b3);
    put16(raw+b0,0xC000);put16(raw+b0+6,0);
    raw[b1+24]=3;
    put32(raw+b1+12,0);
    put16(raw+b1+8,0x217);
    put32(bank_bytes+64u+3u*12u+2u*56u+2u*4u,0x40000000u);
    assert(t3_divmot_open(&bank,bank_bytes,sizeof(bank_bytes)));
    assert(t3_divmot_open(&common,raw,sizeof(raw)));
    assert(t3_divmot_slot_resolve(&bank,&common,2,&move,&owner)==1);
    assert(owner==&common && move.attack_word==0x217);
    assert(t3_move_vm_start(&vm,&bank,&common,2));
    assert(vm.active_bank==&common && vm.slot==2);
    t3_history_reset(&h);
    t3_history_push(&h,input(0,T3_IN_LP));
    /* Main bank LP branch would incorrectly trigger immediately. */
    assert(t3_move_vm_tick(&vm,&h)==0);
    assert(vm.slot==2 && vm.frame==1);
    t3_history_reset(&h);
    t3_history_push(&h,input(0,0));
    assert(t3_move_vm_tick(&vm,&h)==0);
    assert(t3_move_vm_tick(&vm,&h)==0);
    assert(t3_move_vm_tick(&vm,&h)==1);
    assert(vm.slot==0 && vm.active_bank==&bank);
}

static void advance(t3_match_t* match, unsigned frames,t3_input_t a,t3_input_t b) {
    unsigned i;
    for(i=0;i<frames;++i)t3_match_step(match,a,b);
}
static void range(t3_match_t* match) {
    advance(match,32,input(T3_IN_FORWARD,0),input(T3_IN_FORWARD,0));
}

static void test_live_combat_levels(void) {
    t3_match_t match;
    t3_match_reset(&match);
    range(&match);
    /* High attack misses crouching opponent. */
    t3_match_step(&match,input(0,T3_IN_LP),input(T3_IN_CROUCH,0));
    advance(&match,20,input(0,0),input(T3_IN_CROUCH,0));
    assert(match.hits==0 && match.blocks==0);
    t3_match_reset(&match);range(&match);
    /* Mid gets through crouch guard. */
    t3_match_step(&match,input(0,T3_IN_LK),input(T3_IN_CROUCH|T3_IN_GUARD,0));
    advance(&match,12,input(0,0),input(T3_IN_CROUCH|T3_IN_GUARD,0));
    assert(match.hits==1 && match.blocks==0);
    t3_match_reset(&match);range(&match);
    /* Low beats standing guard. */
    t3_match_step(&match,input(0,T3_IN_RK),input(T3_IN_GUARD,0));
    advance(&match,14,input(0,0),input(T3_IN_GUARD,0));
    assert(match.hits==1 && match.blocks==0);
    t3_match_reset(&match);range(&match);
    /* Crouch guard blocks a low. */
    t3_match_step(&match,input(0,T3_IN_RK),input(T3_IN_GUARD|T3_IN_CROUCH,0));
    advance(&match,14,input(0,0),input(T3_IN_GUARD|T3_IN_CROUCH,0));
    assert(match.blocks==1 && match.hits==0);
    t3_match_reset(&match);range(&match);
    /* Jump avoids grounded low attacks for the airborne window. */
    t3_match_step(&match,input(0,T3_IN_RK),input(T3_IN_JUMP,0));
    advance(&match,14,input(0,0),input(0,0));
    assert(match.hits==0 && match.blocks==0);
}

static void test_dash_and_round_history(void) {
    t3_match_t match;
    int32_t x;
    t3_match_reset(&match);
    t3_match_step(&match,input(T3_IN_FORWARD,0),input(0,0));
    t3_match_step(&match,input(0,0),input(0,0));
    x=match.fighters[0].x;
    t3_match_step(&match,input(T3_IN_FORWARD,0),input(0,0));
    assert(match.fighters[0].x-x==T3_WALK_SPEED*2);
    assert(match.fighters[0].dash_frames>0);
    t3_match_reset(&match);
    assert(match.input_histories[0].count==0);
    advance(&match,T3_ROUND_TICKS,input(0,0),input(0,0));
    advance(&match,T3_ROUND_PAUSE,input(0,0),input(0,0));
    assert(match.input_histories[0].count==0);
}

int main(void) {
    test_commands();
    test_bank_and_branch();
    test_vm_execution();
    test_common_bank_branches();
    test_live_combat_levels();
    test_dash_and_round_history();
    puts("DIVMOT input/branch and combat level regressions passed");
    return 0;
}
