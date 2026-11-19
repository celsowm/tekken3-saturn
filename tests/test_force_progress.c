#include "force_progress.h"
#include "psx_round_flow.h"
#include <assert.h>
#include <string.h>

static void check_force_round_end(int boss_phase, int expected_state) {
    t3_psx_round_flow_t flow={.state=T3_ROUND_FIGHT,.played=1,.to_win=1,.max_rounds=1,.human_mask=1};
    t3_psx_round_actor_t actors[2]={
        {.health=100*65536,.maximum=130*65536,.active=1,.length=1},
        {.health=20*65536,.maximum=130*65536,.active=1,.length=1,.is_cpu=1},
    };
    t3_psx_round_flow_facts_t facts={.force=1,.force_level_ended=boss_phase};
    t3_psx_round_flow_actions_t actions;
    assert(t3_psx_round_flow_step(&flow,actors,&facts,&actions));
    assert(flow.state==T3_ROUND_RESULT && actors[0].won==1);
    for(unsigned tick=0;tick<90;++tick)
        assert(t3_psx_round_flow_step(&flow,actors,&facts,&actions));
    assert(flow.state==expected_state && actions.stop_sounds);
}

int main(void){
    check_force_round_end(0,T3_ROUND_END);
    check_force_round_end(1,T3_ROUND_STAGE_CLEAR);
    /* All four regular stages have scripts; only the earned fifth stage
     * enters the direct-boss state. */
    t3_force_level_t setup;
    t3_force_level_setup(&setup,3);
    assert(setup.state==0 && setup.level==3);
    t3_force_level_setup(&setup,4);
    assert(setup.state==8 && setup.level==4);
    const uint8_t tables[T3_FORCE_TABLE_BYTES]={0};
    const uint8_t script[T3_FORCE_SCRIPT_BYTES]={0};
    for(int keys=0;keys<=3;++keys)for(int unlocked=0;unlocked<=1;++unlocked){
        t3_force_level_t lv={.level=3,.state=6,.round_state=8,.keys=keys,
            .key_count=(uint8_t)keys,.dr_b=unlocked,.player_health=130*65536};
        t3_force_level_step(&lv,tables,script);
        assert(lv.state==7);
        assert(lv.key_earned==(!unlocked&&keys<3));
        assert(lv.final_challenge==(!unlocked&&keys==3));
        for(unsigned tick=0;tick<120;++tick)t3_force_level_step(&lv,tables,script);
        assert(lv.keys==keys+(!unlocked&&keys<3));
    }
    uint8_t bytes[T3_FORCE_PROGRESS_BYTES];
    for(unsigned keys=0;keys<=3;++keys)for(unsigned doctor=0;doctor<=1;++doctor){
        t3_force_progress_t a={99999990u,(uint8_t)keys,(uint8_t)doctor},b={0};
        assert(t3_force_progress_pack(&a,bytes));
        assert(t3_force_progress_unpack(bytes,sizeof bytes,&b));
        assert(a.hiscore==b.hiscore&&a.keys==b.keys&&a.doctor==b.doctor);
        for(unsigned i=0;i<sizeof bytes;++i){
            bytes[i]^=1u;
            assert(!t3_force_progress_unpack(bytes,sizeof bytes,&b));
            bytes[i]^=1u;
        }
    }
    assert(!t3_force_progress_unpack(bytes,sizeof bytes-1u,&(t3_force_progress_t){0}));
    assert(!t3_force_progress_pack(&(t3_force_progress_t){0,4,0},bytes));
    assert(!t3_force_progress_pack(&(t3_force_progress_t){100000000u,0,0},bytes));
    const t3_force_handoff_t start={.character=9,.costume=1,.pad_side=1,.bosses={1,2,3,4}};
    for(int level=0;level<5;++level)for(int challenge=0;challenge<=1;++challenge){
        t3_force_level_t lv={.level=level,.flag_70e8=1,.final_challenge=challenge,
            .score=1230,.hiscore_run=4560,.keys=3,.timer=123,.kills=20,.mode_flags=7};
        t3_force_handoff_t next;
        const int expected=level<3||(level==3&&challenge)?T3_FORCE_RUN_NEXT:T3_FORCE_RUN_RESULT;
        assert(t3_force_run_finish(&start,&lv,1,42*65536,12345,&next)==expected);
        assert(next.level==level+(expected==T3_FORCE_RUN_NEXT));
        assert(next.score==1230&&next.hiscore==4560&&next.keys==3&&next.kills==20);
        assert(next.character==9&&next.costume==1&&next.pad_side==1&&next.seed==12345);
        assert(!memcmp(next.bosses,start.bosses,sizeof next.bosses));
        assert(t3_force_run_finish(&start,&lv,0,0,12345,&next)==T3_FORCE_RUN_LOST);
        lv.flag_70e8=0;
        assert(t3_force_run_finish(&start,&lv,1,42*65536,12345,&next)==T3_FORCE_RUN_LOST);
    }
    return 0;
}
