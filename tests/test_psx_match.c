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
    return 0;
}
