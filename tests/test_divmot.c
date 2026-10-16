#include "divmot.h"
#include "move_vm.h"
#include "input_history.h"
#include "fight.h"
#include "psx_movevm_bridge.h"
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

static void test_vm_uses_loaded_animation_length(void) {
    uint8_t bytes[N_BYTES+52u];
    t3_divmot_t bank;
    t3_move_vm_t vm;
    t3_move_vm_t projected;
    t3_input_history_t history;
    const unsigned moves=64u+3u*12u;
    make_bank();
    memcpy(bytes,bank_bytes,N_BYTES);
    memset(bytes+N_BYTES,0,52u); /* tag, seven-frame constant zero pose */
    bytes[N_BYTES+2u]=7;
    for(unsigned i=7;i<15;++i)put32(bytes+4u+4u*i,sizeof(bytes));
    for(unsigned i=0;i<2;++i) {
        put32(bytes+moves+56u*i,2u);
        bytes[moves+56u*i+24u]=0; /* raw retail row before loader fixup */
    }
    assert(t3_divmot_open(&bank,bytes,sizeof(bytes)));
    t3_history_reset(&history);
    t3_history_push(&history,input(0,0));
    assert(t3_move_vm_start(&vm,&bank,NULL,0));
    assert(vm.move.length==7);
    /* Post-hit projection must use the SAME loader normalization as the live
     * VM. Comparing raw row length zero used to reject every retail move. */
    assert(t3_psx_movevm_project(&vm,1,6,&projected));
    assert(projected.move.length==7 && projected.clock.pose_frame==6);
    assert(!t3_psx_movevm_project(&vm,1,8,&projected));
    for(unsigned i=0;i<7;++i)assert(t3_move_vm_tick(&vm,&history)==0);
    assert(vm.clock.pose_frame==7 && vm.transitions==0);
    assert(t3_move_vm_tick(&vm,&history)==1);
    assert(vm.clock.pose_frame==0 && vm.transitions==1);
    t3_history_push(&history,input(0,T3_IN_LP));
    assert(t3_move_vm_tick(&vm,&history)==1);
    assert(vm.slot==1 && vm.move.length==7);
    bytes[N_BYTES+2u]=0; /* malformed present stream cannot look executable */
    assert(!t3_move_vm_start(&vm,&bank,NULL,0));
}

static void test_exe_common_branch_call_return(void) {
    uint8_t common[3u*12u]={0};
    t3_divmot_t bank;
    t3_move_record_t current;
    t3_branch_record_t picked;
    t3_input_history_t history;
    make_bank();
    put16(bank_bytes+64u,0xC00Cu);
    put16(bank_bytes+64u+6u,1u); /* common row 1, not local slot 1 */
    put16(common+12u,1u); /* LP -> move slot 1, frames 4..10 */
    put16(common+12u+6u,1u);
    common[12u+9u]=4;common[12u+10u]=10;
    put16(common+24u,0xC00Du); /* unconditional RETURN */
    assert(t3_divmot_open(&bank,bank_bytes,sizeof(bank_bytes)));
    assert(t3_divmot_slot(&bank,NULL,0,&current)==1);
    t3_history_reset(&history);
    t3_history_push(&history,input(0,T3_IN_LP));
    assert(!t3_divmot_branch_pick(&bank,current,&history,5,&picked));
    assert(!t3_divmot_set_common_branches(&bank,common,sizeof(common)-1u));
    assert(t3_divmot_set_common_branches(&bank,common,sizeof(common)));
    assert(!t3_divmot_branch_pick(&bank,current,&history,3,&picked));
    assert(t3_divmot_branch_pick(&bank,current,&history,5,&picked));
    assert(picked.command==1u && picked.target_slot==1u);
    t3_history_push(&history,input(0,0));
    assert(!t3_divmot_branch_pick(&bank,current,&history,5,&picked));
    assert(t3_divmot_branch_pick(&bank,current,&history,30,&picked));
    assert(picked.command==0xC000u && picked.target_slot==0u);
    put16(common+12u,0xC00Cu); /* nested/cyclic calls cannot loop forever */
    assert(!t3_divmot_branch_pick(&bank,current,&history,30,&picked));
    assert(t3_divmot_set_common_branches(&bank,NULL,0));
    assert(bank.common_branch_count==0);
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
    assert(!t3_command_matches(0xC00D,&h)); /* branch RETURN, not an input */
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
    assert(vm.slot==0 && vm.move.length==30 && vm.clock.pose_frame==0);
    t3_history_reset(&h);
    t3_history_push(&h,input(0,T3_IN_LP));
    assert(t3_move_vm_tick(&vm,&h)==1);
    assert(vm.slot==1 && vm.clock.pose_frame==0 && vm.move.attack_word==0x412);
    /* Attack runs for 10 frames, then follows its default continuation. */
    t3_history_reset(&h);
    t3_history_push(&h,input(0,0));
    for (i=0; i<10; ++i) assert(t3_move_vm_tick(&vm,&h)==0);
    assert(vm.clock.pose_frame==10);
    assert(t3_move_vm_tick(&vm,&h)==1);
    assert(vm.slot==0 && vm.transitions==2);
    /* A freeze advances move time without advancing authored pose time. */
    vm.clock.pose_frame=vm.clock.root_frame=1;
    vm.clock.hit_freeze=3;
    for(i=0;i<2;++i) {
        assert(t3_move_vm_tick(&vm,&h)==0);
        assert(vm.clock.pose_frame==1 && vm.clock.root_frame==1);
    }
    assert(t3_move_vm_tick(&vm,&h)==0 && vm.clock.pose_frame==2);
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
    assert(vm.slot==2 && vm.clock.pose_frame==1);
    t3_history_reset(&h);
    t3_history_push(&h,input(0,0));
    assert(t3_move_vm_tick(&vm,&h)==0);
    assert(t3_move_vm_tick(&vm,&h)==0);
    assert(t3_move_vm_tick(&vm,&h)==1);
    assert(vm.slot==0 && vm.active_bank==&bank);
}

static void test_phased_stream_identity(void) {
    uint8_t bytes[N_BYTES+104u];t3_divmot_t bank;t3_move_vm_t vm;
    const unsigned moves=100u;
    const t3_move_start_facts_t no_hit={0,1,0};
    make_bank();memcpy(bytes,bank_bytes,N_BYTES);memset(bytes+N_BYTES,0,104);
    bytes[N_BYTES+2]=bytes[N_BYTES+54]=7;
    for(unsigned i=7;i<15;++i)put32(bytes+4u+4u*i,sizeof(bytes));
    put32(bytes+moves,2);put32(bytes+moves+56,54);
    assert(t3_divmot_open(&bank,bytes,sizeof(bytes)));
    assert(t3_move_vm_start_phased(&vm,&bank,NULL,0));
    vm.clock.pose_frame=vm.clock.root_frame=vm.clock.move_frame=4;
    vm.clock.move_changed=0;
    assert(t3_move_vm_queue(&vm,1,2,2,4,0));
    assert(t3_move_vm_begin(&vm,no_hit)==1);
    assert(vm.clock.pose_frame==5 && vm.clock.move_changed==1 && vm.clock.move_frame==1);
    /* Two moves referencing the SAME stream retain the original age rule. */
    put32(bytes+moves+56,2);
    assert(t3_move_vm_start_phased(&vm,&bank,NULL,0));
    vm.clock.pose_frame=vm.clock.root_frame=vm.clock.move_frame=4;vm.clock.move_changed=0;
    assert(t3_move_vm_queue(&vm,1,2,2,4,0) && t3_move_vm_begin(&vm,no_hit)==1);
    assert(vm.clock.move_changed==0 && vm.clock.move_frame==5);
}
static void test_phased_move_lifecycle(void) {
    t3_divmot_t bank;
    t3_move_vm_t vm,unchanged;
    t3_input_history_t history;
    const t3_move_start_facts_t no_hit={0,1,0};
    const unsigned row0=64u,row1=64u+3u*12u+56u;
    make_bank();bank_bytes[row0+8]=14; /* verified high-hit clock transition */
    bank_bytes[row1+27]=1; /* authored held pose for root-ownership regression */
    assert(t3_divmot_open(&bank,bank_bytes,sizeof(bank_bytes)));
    t3_history_reset(&history);t3_history_push(&history,input(0,T3_IN_LP));
    assert(t3_move_vm_start_phased(&vm,&bank,NULL,0));
    assert(vm.clock.pose_frame==1 && vm.clock.root_frame==1);
    unchanged=vm;
    assert(t3_move_vm_tick(&vm,&history)==-1);
    assert(!memcmp(&vm,&unchanged,sizeof(vm)));
    assert(t3_move_vm_begin(&vm,no_hit)==0 && vm.clock.pose_frame==2);
    assert(t3_move_vm_queue_input(&vm,&history,NULL,NULL,0)==1);
    assert(vm.slot==0 && vm.pending_slot==1 && vm.pending_valid && vm.transitions==0);
    assert(t3_move_vm_begin(&vm,no_hit)==1);
    assert(vm.slot==1 && vm.root_slot==1 && vm.clock.pose_frame==1 && vm.clock.move_frame==1);
    assert(vm.current_transition==14 && vm.transitions==1 && !vm.pending_valid);

    assert(t3_move_vm_start_phased(&vm,&bank,NULL,0));
    assert(t3_move_vm_queue(&vm,1,6,2,5,0));
    for(unsigned i=2;i<=5;++i) {
        assert(t3_move_vm_begin(&vm,no_hit)==0);
        assert(vm.slot==0 && vm.clock.pose_frame==(int)i);
    }
    assert(t3_move_vm_begin(&vm,no_hit)==1 && vm.slot==1 && vm.clock.pose_frame==1);

    assert(t3_move_vm_start_phased(&vm,&bank,NULL,0));
    vm.clock.pose_frame=vm.clock.root_frame=7;
    assert(t3_move_vm_queue(&vm,1,4,2,1,2));
    assert(t3_move_vm_begin(&vm,no_hit)==1);
    assert(vm.slot==1 && vm.root_slot==0 && vm.clock.pose_frame==1 && vm.clock.root_frame==8);
    assert(t3_move_vm_begin(&vm,no_hit)==0 && vm.clock.pose_frame==1 && vm.clock.root_frame==9);
    assert(t3_move_vm_begin(&vm,no_hit)==0 && vm.clock.pose_frame==1 && vm.clock.root_frame==10);
    assert(t3_move_vm_begin(&vm,no_hit)==0 && vm.clock.pose_frame==2 && vm.clock.root_frame==2);
    assert(vm.root_slot==1); /* release binds rootMove to the current poseMove */

    assert(t3_move_vm_start_phased(&vm,&bank,NULL,1));
    vm.clock.frame_step=-1;vm.clock.pose_frame=vm.clock.root_frame=1;
    t3_history_reset(&history);t3_history_push(&history,input(0,0));
    assert(t3_move_vm_queue_input(&vm,&history,NULL,NULL,0)==1);
    assert(vm.pending_slot==0 && vm.queued.branch_kind==4);
    assert(t3_move_vm_begin(&vm,no_hit)==1 && vm.slot==0 && vm.clock.pose_frame==1);
    assert(t3_move_vm_queue(&vm,1,30,2,1,0));
    unchanged=vm;
    assert(t3_move_vm_begin(&vm,no_hit)==-1); /* launch physics is not guessed */
    assert(!memcmp(&vm,&unchanged,sizeof(vm)));

    /* A pressed command during the END row's early look-ahead window
     * queues the continuation stance's command without starting it yet. */
    bank_bytes[row0+24+9]=5;bank_bytes[row0+24+10]=10;
    assert(t3_move_vm_start_phased(&vm,&bank,NULL,1));
    vm.clock.pose_frame=vm.clock.root_frame=4;
    t3_history_reset(&history);t3_history_push(&history,input(0,T3_IN_LP));
    assert(t3_move_vm_queue_input(&vm,&history,NULL,NULL,0)==1);
    assert(vm.slot==1 && vm.pending_valid && vm.queued.branch_kind==3);
    assert(vm.queued.entry_frame==5 && vm.queued.branch_window==25);
    assert(t3_move_vm_begin(&vm,no_hit)==0 && vm.clock.pose_frame==5);
    assert(t3_move_vm_begin(&vm,no_hit)==1 && vm.clock.pose_frame==1);
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
    test_vm_uses_loaded_animation_length();
    test_exe_common_branch_call_return();
    test_common_bank_branches();
    test_phased_move_lifecycle();
    test_phased_stream_identity();
    test_live_combat_levels();
    test_dash_and_round_history();
    puts("DIVMOT input/branch and combat level regressions passed");
    return 0;
}
