#include "psx_round_flow.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <limits.h>
#include <string.h>

static t3_psx_round_flow_t s;
static t3_psx_round_actor_t a[2];
static t3_psx_round_flow_facts_t f;
static t3_psx_round_flow_actions_t x;
static void reset(void) {
    s=(t3_psx_round_flow_t){.state=T3_ROUND_FIGHT,.played=1,.timer=2400,
        .to_win=2,.max_rounds=3,.human_mask=3,.draw_wins=1,.camera=2,.input={1,1}};
    a[0]=a[1]=(t3_psx_round_actor_t){.health=140*65536,.maximum=140*65536,
        .active=1,.pose=1,.length=54,.state_word=6482};
    f=(t3_psx_round_flow_facts_t){.replay_mode=5};
}
static void step(void){assert(t3_psx_round_flow_step(&s,a,&f,&x));}
int main(void) {
    reset();s.state=T3_ROUND_INTRO;s.frame=149;
    step();assert(s.state==T3_ROUND_INTRO && s.frame==150 && s.timer==2400);
    step();assert(s.state==T3_ROUND_FIGHT && s.timer==2400);
    s.played=2;s.state=T3_ROUND_INTRO;s.frame=89;
    step();assert(s.state==T3_ROUND_INTRO);step();assert(s.state==T3_ROUND_FIGHT);
    reset();s.timer=1;step();assert(s.timer==0 && s.state==T3_ROUND_FIGHT && x.fight_clock_advance);
    step();assert(s.state==T3_ROUND_RESULT && s.result==33 && s.max_rounds==2);
    assert(s.hold==90 && s.counter==0 && s.no_damage && a[0].wins==1 && a[1].wins==1);
    assert(a[0].carried_health==a[0].health && !x.fight_clock_advance);
    for(unsigned i=0;i<89;++i)step();
    assert(s.state==T3_ROUND_RESULT && s.counter==89 && s.hold==90 && s.input[0]==2);
    step();assert(s.state==T3_ROUND_REPLAY_DONE && s.counter==90 && s.replay_request==1);
    f.replay_mode=1;step();assert(s.state==T3_ROUND_REPLAY_DONE);
    f.replay_mode=5;s.replay_request=0;step();
    assert(s.state==T3_ROUND_WIN_WAIT && s.counter==0 && s.hold==299 && s.camera==2);
    assert(x.start_slot[0]==-1 && x.start_slot[1]==-1 && a[0].active && a[1].active);
    for(unsigned i=0;i<74;++i)step();
    assert(s.state==T3_ROUND_WIN_WAIT);
    step();assert(s.state==T3_ROUND_END && s.done && s.counter==75 && s.hold==224);
    reset();a[1].health=0;step();assert(s.result==10 && s.hold==240 && a[0].won==1);
    /* Perfect-KO display ends on camera counter 90, not by draining hold 240. */
    for(unsigned i=0;i<90;++i)step();
    assert(s.state==T3_ROUND_REPLAY_DONE && s.hold==240);
    s.replay_request=0;f.frame_counter=1;step();
    assert(x.start_slot[0]==3432 && x.grounded_mask==1 && x.clear_blends);
    assert(x.animate_before_camera && s.camera==3 && a[0].active && !a[1].active);
    assert(a[0].win_pose==2 && s.state==T3_ROUND_WIN_WAIT && s.hold==299);
    s.camera=5;a[0].length=118;a[0].pose=116;step();assert(s.state==T3_ROUND_WIN_WAIT);
    a[0].pose=117;step();assert(s.state==T3_ROUND_END && s.win_done[0] && s.win_done[1]);
    /* Human physical face buttons override parity in square/triangle/cross/circle order. */
    for(unsigned i=0;i<4;++i){
        reset();s.state=T3_ROUND_REPLAY_DONE;a[1].won=1;a[0].won=-1;
        const unsigned buttons[4]={0xF0,0x70,0x60,0x20};f.physical[1]=(uint16_t)buttons[i];
        step();assert(x.start_slot[1]==3431+(int)i && s.camera==4 && !a[0].active && a[1].active);
    }
    reset();s.state=T3_ROUND_REPLAY_DONE;s.camera=7;s.match_result=2;
    a[1].wins=2;a[1].won=1;a[0].won=-1;
    step();assert(s.camera==4 && x.start_slot[1]==3431);
    reset();s.state=T3_ROUND_REPLAY_DONE;a[0].wins=2;a[0].won=1;
    a[0].is_cpu=1;a[1].won=-1;a[1].health=0;
    step();assert(s.camera==7 && x.start_slot[1]==3429 && !a[0].active && a[1].active);
    s.counter=59;step();assert(s.state==T3_ROUND_END && s.counter==60);
    reset();s.state=T3_ROUND_REPLAY_DONE;a[1].wins=2;a[1].won=1;
    a[1].is_cpu=1;a[0].won=-1;
    step();assert(s.camera==6 && x.start_slot[0]==3436 && a[0].active && !a[1].active);
    reset();s.state=T3_ROUND_REPLAY_DONE;f.replay_skip=1;step();assert(s.state==T3_ROUND_END);
    reset();s.state=T3_ROUND_REPLAY_DONE;s.match_result=1;a[0].won=1;a[1].won=-1;
    f.replay_skip=1;step();assert(s.state==T3_ROUND_WIN_WAIT);
    f.pressed[1]=0x800;step();assert(s.state==T3_ROUND_END);
    reset();s.timer_stopped=1;step();assert(s.timer==2400 && s.frame==1 && x.fight_clock_advance);
    reset();s.state=T3_ROUND_RESULT;s.result=1;a[0].won=1;a[1].won=-1;
    a[0].state_word=4;step();assert(a[0].script_input==0x1000 && a[1].script_input==0);
    a[0].bank_type=18;step();assert(a[0].script_input==0);
    reset();s.counter=INT32_MAX;
    t3_psx_round_flow_t before=s;t3_psx_round_actor_t pair[2]={a[0],a[1]};
    t3_psx_round_flow_actions_t actions=x;
    assert(!t3_psx_round_flow_step(&s,a,&f,&x));
    assert(!memcmp(&s,&before,sizeof(s)) && !memcmp(a,pair,sizeof(a)) && !memcmp(&x,&actions,sizeof(x)));
    reset();s.state=7;before=s;assert(!t3_psx_round_flow_step(&s,a,&f,&x));assert(!memcmp(&s,&before,sizeof(s)));
    return 0;
}
