#include "psx_match.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

enum { B0=64,B1=B0+36,B2=B1+112,B6=B2+4023*4,BYTES=B6+52 };
static uint8_t bytes[BYTES],trig[0x2800],atan_bytes[1025],push[20];
static t3_psx_replay_storage_t history,history_before;
static void p16(uint8_t* p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t* p,uint32_t v){p16(p,(uint16_t)v);p16(p+2,(uint16_t)(v>>16));}
/* SituationClassify's four posture rows and four opponent quadrants. */
static void test_throw_situation(void) {
    static const uint16_t state[4]={0x40u,0x20u,0x80u,0x280u};
    static const int16_t expected[4][4]={{2,4,3,5},{6,8,7,9},{10,12,11,13},{14,16,15,17}};
    t3_psx_ground_state_t self={0},opp={0};
    for(unsigned row=0;row<4;++row)for(unsigned q=0;q<4;++q) {
        self.opponent_quadrant=(uint8_t)q;opp.state=state[row];
        self.relative_angle=0;
        assert(t3_psx_throw_situation(&self,1,&opp)==expected[row][q]);
    }
    self.opponent_quadrant=0;opp.state=0x40u;
    assert(t3_psx_throw_situation(&self,0,&opp)==-1);
    opp.in_air=1;assert(t3_psx_throw_situation(&self,1,&opp)==-1);opp.in_air=0;
    opp.about_to_hit=1;assert(t3_psx_throw_situation(&self,1,&opp)==-1);opp.about_to_hit=0;
    self.opponent_quadrant=4;assert(t3_psx_throw_situation(&self,1,&opp)==-1);
    /* No posture row (state bits 0x20/0x40/0x80 clear). */
    self.opponent_quadrant=0;opp.state=0;assert(t3_psx_throw_situation(&self,1,&opp)==-1);
    /* A negative angle is the same as its 16-bit predecessor. */
    opp.state=0x40u;self.relative_angle=0xFFFFu;assert(t3_psx_throw_situation(&self,1,&opp)==2);
    /* Behind: quadrant 0/2 only, and only with state bit 1 set. */
    self.relative_angle=0x6000u;
    for(unsigned q=0;q<4;++q) {
        self.opponent_quadrant=(uint8_t)q;opp.state=2u;
        assert(t3_psx_throw_situation(&self,1,&opp)==(q==0?0x12:q==2?0x13:-1));
    }
    opp.state=0;assert(t3_psx_throw_situation(&self,1,&opp)==-1);
    self.relative_angle=0x5000u;opp.state=2u;self.opponent_quadrant=0;
    assert(t3_psx_throw_situation(&self,1,&opp)==-1);
}
/* Transition 0x22 writes +0xBD from the victim's pre-restart pose/startup. */
static void test_attack_pending(void) {
    t3_psx_ground_tables_t t={{trig,sizeof(trig)},atan_bytes,sizeof(atan_bytes),push,sizeof(push),NULL};
    t3_move_record_t previous={0},current={0};
    t3_move_vm_t vm={0};
    t3_psx_ground_state_t victim,thrower;
    current.length=4;current.state_word=0x3884u;
    vm.move=current;vm.current_transition=34u;
    previous.damage_word=0x4000u;previous.active_first=10u;
    memset(&victim,0,sizeof(victim));victim.root=(t3_vec3i_t){-100,0,0};
    memset(&thrower,0,sizeof(thrower));thrower.root=(t3_vec3i_t){100,0,0};
    assert(t3_psx_ground_start(&victim,&thrower,&vm,&previous,&t,0,0,
                               (t3_move_start_facts_t){0,1,0,5},NULL,0));
    assert(victim.attack_pending==1u && victim.throw_state==-1 && victim.in_throw==1u);
    memset(&victim,0,sizeof(victim));victim.root=(t3_vec3i_t){-100,0,0};
    assert(t3_psx_ground_start(&victim,&thrower,&vm,&previous,&t,0,0,
                               (t3_move_start_facts_t){0,1,0,10},NULL,0));
    assert(victim.attack_pending==0u);
    memset(&victim,0,sizeof(victim));victim.root=(t3_vec3i_t){-100,0,0};
    previous.damage_word=0;
    assert(t3_psx_ground_start(&victim,&thrower,&vm,&previous,&t,0,0,
                               (t3_move_start_facts_t){0,1,0,0},NULL,0));
    assert(victim.attack_pending==0u);
    previous.damage_word=0x4000u;thrower.power_timer=1;
    memset(&victim,0,sizeof(victim));victim.root=(t3_vec3i_t){-100,0,0};
    assert(t3_psx_ground_start(&victim,&thrower,&vm,&previous,&t,0,0,
                               (t3_move_start_facts_t){0,1,0,10},NULL,0));
    assert(victim.attack_pending==1u);
    thrower.power_timer=0;
    memset(&victim,0,sizeof(victim));victim.root=(t3_vec3i_t){-100,0,0};
    victim.throw_state=-1;victim.attack_pending=7u;
    assert(t3_psx_ground_start(&victim,&thrower,&vm,&previous,&t,0,0,
                               (t3_move_start_facts_t){0,1,0,0},NULL,0));
    assert(victim.attack_pending==7u);
}
int main(void) {
    t3_divmot_t bank;
    t3_psx_match_t game,before;
    t3_psx_match_config_t c={0};
    t3_input_t neutral={0,0},forward={T3_IN_FORWARD,0};
    memset(bytes,0,sizeof(bytes));bytes[1]=0;p16(bytes+2,2);
    p32(bytes+4,B0);p32(bytes+8,B1);p32(bytes+12,B2);
    for(unsigned i=3;i<7;++i)p32(bytes+4+4*i,B6);
    for(unsigned i=7;i<15;++i)p32(bytes+4+4*i,BYTES);
    for(unsigned i=0;i<4023;++i)p32(bytes+B2+4*i,T3_DIVMOT_EMPTY_SLOT);
    p32(bytes+B2+3*4,0);p32(bytes+B2+21*4,1);
    for(unsigned i=0;i<2;++i) {
        uint8_t* row=bytes+B1+56*i;
        p32(row,2);p32(row+4,0x842);p32(row+12,i?2:0);row[24]=4;
    }
    p16(bytes+B1+56+22,(uint16_t)-256);bytes[B1+56+25]=1;bytes[B1+56+26]=4;
    bytes[B6+2]=4;
    p16(bytes+B0,0x410);p16(bytes+B0+6,21);bytes[B0+8]=6;bytes[B0+10]=4;bytes[B0+11]=1;
    p16(bytes+B0+12,0xC000);p16(bytes+B0+18,3);bytes[B0+20]=6;
    p16(bytes+B0+24,0xC000);p16(bytes+B0+30,3);bytes[B0+32]=6;
    p16(trig+0x800,4096);p16(trig+0x1800,(uint16_t)-4096);
    assert(t3_divmot_open(&bank,bytes,sizeof(bytes)));
    c.bank=&bank;c.ground_tables=(t3_psx_ground_tables_t){{trig,sizeof(trig)},atan_bytes,sizeof(atan_bytes),push,sizeof(push),NULL};
    c.scale_percent=100;c.max_health_q16=140*65536;
    c.spawn[0]=(t3_psx_spawn_t){{-100,0,0},{-100,0,0},0x4000,0x4000};
    c.spawn[1]=(t3_psx_spawn_t){{100,0,0},{100,0,0},0xC000,0xC000};
    assert(t3_psx_match_init(&game,&c));
    assert(t3_psx_match_step(&game,forward,neutral));
    assert(game.ticks==1 && game.fighter[0].ground.root.x==-100);
    assert(game.fighter[0].vm.pending_valid && game.fighter[0].vm.slot==3);
    assert(game.fighter[0].input.count==1);
    assert(t3_psx_match_step(&game,forward,neutral));
    assert(game.fighter[0].vm.slot==21 && game.fighter[0].ground.root.x==-99);
    assert(game.fighter[0].ground.step_accum_x==1);
    assert(game.fighter[0].health_q16==140*65536); /* no prototype HP owner */
    game.fighter[0].vm.pending_valid=0;
    assert(t3_move_vm_queue(&game.fighter[0].vm,3,30,2,0,0));
    before=game;
    assert(!t3_psx_match_step(&game,forward,neutral));
    assert(!memcmp(&game,&before,sizeof(game)));
    assert(t3_psx_match_init(&game,&c));
    assert(t3_psx_match_step(&game,forward,neutral));
    assert(t3_psx_match_step(&game,(t3_input_t){T3_PSX_INPUT_CLEAR|T3_IN_LP,T3_IN_LP},neutral));
    assert(game.fighter[0].ground.root.x==-99); /* physics used the preceding input */
    assert(game.fighter[0].input.direction_cleared);
    assert(!t3_history_at(&game.fighter[0].input,0).pressed_buttons);
    assert(!game.fighter[0].vm.pending_valid); /* raw clear isn't a neutral return */
    c.enable_rounds=1;
    assert(t3_psx_match_init(&game,&c));
    game.fighter[1].health_q16=0;
    assert(t3_psx_match_step(&game,neutral,neutral));
    assert(game.round.done && game.round.wins[0]==1 && game.round.hold==240);
    for(unsigned i=0;i<239;++i)assert(t3_psx_match_step(&game,forward,neutral));
    assert(game.round.done && game.fighter[1].health_q16==0);
    assert(game.fighter[0].input.direction_cleared && game.fighter[1].input.direction_cleared);
    assert(t3_psx_match_step(&game,neutral,neutral));
    assert(!game.round.done && game.round.number==2 && game.round.wins[0]==1);
    assert(game.fighter[1].health_q16==140*65536 && game.ticks==241);
    assert(game.fighter[0].input.count==1 && !game.fighter[0].hit_done);
    assert(game.fighter[0].ground.root.x==-100 && game.fighter[1].ground.root.x==100);
    assert(game.fighter[0].vm.slot==3 && game.fighter[1].vm.slot==3);
    c.enable_rounds=2;
    assert(t3_psx_match_init(&game,&c));
    assert(!t3_psx_match_step(&game,neutral,neutral));
    t3_psx_match_frame_t video={1,{0,0},{0,0}};
    assert(t3_psx_match_step_original(&game,neutral,neutral,&video));
    assert(game.ticks==1 && game.flow.timer==2399 && game.fight_clock==1);
    game.flow.timer=0;
    assert(t3_psx_match_step_original(&game,forward,forward,&video));
    assert(game.flow.state==T3_ROUND_RESULT && game.round_actor[0].wins==1 && game.round_actor[1].wins==1);
    assert(game.fighter[0].input.direction_cleared && game.fighter[1].input.direction_cleared);
    for(unsigned i=0;i<90;++i)assert(t3_psx_match_step_original(&game,forward,forward,&video));
    assert(game.flow.state==T3_ROUND_REPLAY_DONE && game.flow.replay_request);
    assert(t3_psx_match_step_original(&game,forward,forward,&video));
    assert(game.flow.state==T3_ROUND_WIN_WAIT && game.replay.mode==5 && game.flow.hold==299);
    game.flow.state=T3_ROUND_END;
    /* FightMain states 5 and 6 run no FightFrame; the clock still advances. */
    for(unsigned i=0;i<2;++i) {
        const uint32_t ticks=game.ticks;
        assert(t3_psx_match_step_original(&game,neutral,neutral,&video));
        assert(game.flow.state==T3_ROUND_END && game.ticks==ticks+1 && game.round_gap==i+1);
    }
    before=game;
    /* The public synthetic bank initially lacks the required intro slot. */
    assert(!t3_psx_match_step_original(&game,neutral,neutral,&video));
    assert(!memcmp(&game,&before,sizeof(game)));
    p32(bytes+B2+1*4,0);
    assert(t3_psx_match_step_original(&game,forward,forward,&video));
    assert(game.flow.played==2 && game.flow.state==T3_ROUND_INTRO && game.flow.frame==0); /* placed; FightFrame resumes next */
    assert(game.flow.timer==2400 && game.round_actor[0].wins==1 && game.round_actor[1].wins==1);
    assert(game.fighter[0].health_q16==c.max_health_q16 && game.fighter[1].health_q16==c.max_health_q16);
    assert(game.fighter[0].ground.anchor.x==-1000 && game.fighter[1].ground.anchor.x==1000);
    assert(game.fighter[0].input.direction_cleared && game.fighter[1].input.direction_cleared);
    assert(t3_psx_match_step_original(&game,forward,forward,&video));
    assert(game.flow.state==T3_ROUND_INTRO && game.flow.frame==1);
    assert(game.flow.camera==1 && !game.flow.no_damage);
    game.flow.state=T3_ROUND_REPLAY_DONE;game.flow.result=10;game.flow.replay_request=1;
    game.replay.mark=0;before=game;
    assert(!t3_psx_match_step_original(&game,neutral,neutral,&video));
    assert(!memcmp(&game,&before,sizeof(game)));
    c.replay_storage=&history;
    assert(t3_psx_match_init(&game,&c));
    assert(t3_psx_match_step_original(&game,neutral,neutral,&video));
    assert(history.epoch==1 && history.ring[1].epoch==1 && history.ring[1].actor[0].pose_frame==1);
    game.flow.state=T3_ROUND_REPLAY_DONE;game.flow.replay_request=1;game.replay.mark=0;
    before=game;history_before=history;
    assert(!t3_psx_match_step_original(&game,neutral,neutral,&video));
    assert(!memcmp(&game,&before,sizeof(game)) && !memcmp(&history,&history_before,sizeof(history)));
    assert(t3_psx_match_init(&game,&c) && history.epoch==2);
    assert(!t3_psx_replay_at(&history,&game.replay,1));
    game.flow.state=T3_ROUND_END;game.flow.match_result=3;before=game;
    assert(t3_psx_match_step_original(&game,forward,forward,&video));
    assert(game.ticks==before.ticks+1 && !memcmp(game.fighter,before.fighter,sizeof(game.fighter)));
    assert(!memcmp(&game.replay,&before.replay,sizeof(game.replay)));
    /* Production startup fails closed without the authored entry slots. */
    c.start_intro=1;c.start_seed=1;c.start_physical[0]=0x80;c.start_physical[1]=0x40;
    before=game;
    assert(!t3_psx_match_init(&game,&c));
    assert(!memcmp(&game,&before,sizeof(game)));
    p32(bytes+B2,0);p32(bytes+B2+2*4,0);
    assert(t3_psx_match_init(&game,&c));
    assert(game.flow.played==1 && game.flow.frame==0 && game.flow.state==T3_ROUND_INTRO);
    assert(game.fighter[0].vm.pending_valid && game.fighter[0].vm.pending_slot==0);
    assert(game.fighter[1].vm.pending_valid && game.fighter[1].vm.pending_slot==2);
    for(unsigned tick=1;tick<=150;++tick) {
        video.frame_counter=tick+1;
        assert(t3_psx_match_step_original(&game,forward,forward,&video));
        assert(game.flow.state==T3_ROUND_INTRO && game.flow.timer==2400);
        assert(game.fighter[0].input.direction_cleared && game.fighter[1].input.direction_cleared);
        assert(!game.flow.input[0] && !game.flow.input[1]);
    }
    assert(t3_psx_match_step_original(&game,forward,forward,&video));
    assert(game.flow.frame==151 && game.flow.state==T3_ROUND_FIGHT && game.flow.timer==2400);
    assert(game.fighter[0].vm.pending_valid && game.fighter[0].vm.pending_slot==21);
    assert(t3_psx_match_step_original(&game,forward,forward,&video));
    assert(game.fighter[0].vm.slot==21 && game.flow.timer==2399);
    test_throw_situation();
    test_attack_pending();
    return 0;
}
