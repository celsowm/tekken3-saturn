/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Normal VS RoundFlow adapted from pinned Tekken3Remake round_flow.gd.
 * Changes: typed portable state, explicit external facts and move/render
 * actions, pair-atomic failure, no mode-specific/audio/effect implementation.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "psx_round_flow.h"
#include "psx_round.h"
#include <limits.h>

static int start_pressed(const t3_psx_round_flow_t* s,const t3_psx_round_flow_facts_t* f) {
    unsigned word=0;
    for(unsigned i=0;i<2;++i)if(s->human_mask&(1u<<i))word|=f->pressed[i];
    return (word&0x800u)!=0;
}
static void win_wait(t3_psx_round_flow_t* s,t3_psx_round_actor_t a[2],
                     const t3_psx_round_flow_facts_t* f) {
    s->blend_hold=2;
    if((s->camera==2 && s->counter==75) ||
       ((s->camera==6 || s->camera==7) && s->counter==60))s->done=1;
    if(s->camera==5) {
        for(unsigned i=0;i<2;++i)if(a[i].win_pose<1 || a[i].length-1<=a[i].pose)s->win_done[i]=1;
        if(s->win_done[0] && s->win_done[1])s->done=1;
    }
    if(s->hold>0)--s->hold;
    else s->hold=0;
    if(s->done || s->hold<1 || (!s->match_result && f->replay_skip && s->counter>1) ||
       start_pressed(s,f))s->state=T3_ROUND_END;
}
static void win_start(t3_psx_round_flow_t* s,t3_psx_round_actor_t a[2],
                      const t3_psx_round_flow_facts_t* f) {
    s->hold=300;s->win_done[0]=s->win_done[1]=0;
    s->counter=0;s->done=0;++s->state;
    win_wait(s,a,f);
}
static void camera(t3_psx_round_flow_t* s,t3_psx_round_actor_t a[2],
                   const t3_psx_round_flow_facts_t* f) {
    int selected=-1;
    int assigned=0;
    if(!f->team_round && (a[0].wins>=s->to_win || a[1].wins>=s->to_win)) {
        if(!a[0].is_cpu && a[0].win_pose>0){s->camera=3;selected=0;assigned=1;}
        else if(a[1].is_cpu || a[1].win_pose<1) {
            if(a[0].win_pose>0){s->camera=7;selected=1;}
            else if(a[1].win_pose>0){s->camera=6;selected=0;}
            else s->camera=2;
            assigned=1;
        }
    }
    if(!assigned) {
        if(a[0].win_pose>0){s->camera=3;selected=0;}
        else if(a[1].win_pose>0){s->camera=4;selected=1;}
        else s->camera=2;
    }
    a[0].active=selected<0 || selected==0;
    a[1].active=selected<0 || selected==1;
}
static int result(t3_psx_round_flow_t* s,t3_psx_round_actor_t a[2]) {
    uint16_t left[2];
    for(unsigned i=0;i<2;++i) {
        int32_t unit=a[i].maximum>>12;
        left[i]=(uint16_t)(unit?((int64_t)a[i].health+unit-1)/unit:0);
        a[i].health_left=left[i];
    }
    s->result=s->timer<1?1:0;
    if(left[0]==left[1])s->result|=left[0]?0x20:0x26;
    else if(!left[0] || !left[1]) {
        unsigned health=left[0]>left[1]?left[0]:left[1];
        s->result|=health==4096?10:health<0xCD?0x12:2;
    }
    t3_psx_round_score_t score={0};
    score.wins[0]=(uint16_t)a[0].wins;score.wins[1]=(uint16_t)a[1].wins;
    score.played=(uint16_t)s->played;score.to_win=(uint16_t)s->to_win;
    score.max_rounds=(int16_t)s->max_rounds;score.human_mask=(uint8_t)s->human_mask;
    score.tie_choice=(uint8_t)s->tie_choice;score.draw_wins=(uint8_t)s->draw_wins;
    score.match_result=(uint8_t)s->match_result;score.match_winner=(int8_t)s->match_winner;
    score.match_loser=(int8_t)s->match_loser;
    if(!t3_psx_round_award(&score,left[0],left[1]))return 0;
    s->max_rounds=score.max_rounds;s->match_result=score.match_result;
    s->match_winner=score.match_winner;s->match_loser=score.match_loser;
    for(unsigned i=0;i<2;++i){
        a[i].wins=score.wins[i];a[i].won=score.round_won[i];a[i].carried_health=a[i].health;
    }
    if(s->result&1) {
        if(a[0].won==a[1].won)a[0].win_pose=a[1].win_pose=0;
        else for(unsigned i=0;i<2;++i)if(a[i].won<0)a[i].win_pose=a[i].won;
        s->input[0]=s->input[1]=0;
    }
    s->win_done[0]=s->win_done[1]=0;
    s->hold=(s->result&1)?90:240;s->state=T3_ROUND_RESULT;s->done=0;
    return 1;
}
int t3_psx_round_flow_step(t3_psx_round_flow_t* out,t3_psx_round_actor_t actors[2],
                         const t3_psx_round_flow_facts_t* f,t3_psx_round_flow_actions_t* actions) {
    t3_psx_round_flow_t s;
    t3_psx_round_actor_t a[2];
    t3_psx_round_flow_actions_t x={{-1,-1},0,0,0,0,0};
    if(!out || !actors || !f || !actions)return 0;
    s=*out;a[0]=actors[0];a[1]=actors[1];
    if(s.state<0 || (s.state>6 && s.state!=9) || s.counter<0 || s.counter==INT32_MAX ||
       s.frame<0 || s.frame==INT32_MAX || s.played<1 || s.played>9 || s.timer<0 ||
       s.to_win<1 || s.to_win>5 || s.max_rounds<0 || s.max_rounds>9 ||
       s.human_mask<0 || s.human_mask>3 || s.tie_choice<0 || s.tie_choice>1 ||
       s.draw_wins<0 || s.draw_wins>1 || s.match_result<0 || s.match_result>3)return 0;
    for(unsigned i=0;i<2;++i)if(a[i].health<0 || a[i].maximum<1 || a[i].health>a[i].maximum ||
       a[i].wins<0 || a[i].wins>6 || a[i].length<1 || a[i].pose<0 ||
       a[i].costume==0x26 || (a[i].is_cpu!=0 && a[i].is_cpu!=1))return 0;
    if(s.state>1)++s.counter;
    switch(s.state) {
    case T3_ROUND_INTRO:
        ++s.frame;
        if(s.frame>=(s.played==1?151:91)){s.input[0]=s.input[1]=1;s.state=T3_ROUND_FIGHT;}
        break;
    case T3_ROUND_FIGHT:
        ++s.frame;s.no_damage=!a[0].health || !a[1].health || s.timer<1;
        if(s.no_damage){if(!result(&s,a))return 0;}
        else {if(!s.timer_stopped)--s.timer;x.fight_clock_advance=1;}
        break;
    case T3_ROUND_RESULT:
        for(unsigned i=0;i<2;++i) {
            if(!a[i].health || a[i].won<0){s.input[i]=2;a[i].script_input=0;}
            else if(!(s.result&1) && !a[i].is_cpu)s.input[i]=1;
            else {s.input[i]=2;a[i].script_input=a[i].bank_type!=18 && (a[i].state_word&0x204)?0x1000:0;}
        }
        if((s.result&0x1F) && s.counter==90)s.done=1;
        if(s.done){s.input[0]=s.input[1]=0;s.state=T3_ROUND_REPLAY_DONE;s.replay_request=1;x.stop_sounds=1;}
        break;
    case T3_ROUND_REPLAY:
        s.state=T3_ROUND_REPLAY_DONE;s.replay_request=1;
        break;
    case T3_ROUND_REPLAY_DONE:
        if(f->replay_mode!=5)break;
        x.stop_sounds=1;
        s.blend_hold=2;
        if(!s.match_result && (start_pressed(&s,f) || f->replay_skip)){s.state=T3_ROUND_END;break;}
        for(unsigned i=0;i<2;++i)if(a[i].won>0) {
            unsigned button=f->frame_counter&1u;
            if(f->physical[i]&0x20u)button=3;
            if(f->physical[i]&0x40u)button=2;
            if(f->physical[i]&0x10u)button=1;
            if(f->physical[i]&0x80u)button=0;
            a[i].win_pose=(int32_t)(f->frame_counter&1u)+1;
            x.start_slot[i]=(int16_t)(3431u+button);x.grounded_mask|=(uint8_t)(1u<<i);
            a[i].pose=1;
            break;
        }
        x.animate_before_camera=(s.result&1)==0;
        camera(&s,a,f);
        if(s.camera==6 || s.camera==7)for(unsigned i=0;i<2;++i)if(a[i].won<0){
            x.start_slot[i]=(int16_t)(a[i].health?3436:3429);a[i].pose=1;
        }
        x.clear_blends=1;s.state=T3_ROUND_WIN_POSES;
        win_start(&s,a,f);
        break;
    case T3_ROUND_WIN_POSES:win_start(&s,a,f);break;
    case T3_ROUND_WIN_WAIT:win_wait(&s,a,f);break;
    case T3_ROUND_END:break;
    default:return 0;
    }
    *out=s;actors[0]=a[0];actors[1]=a[1];*actions=x;return 1;
}
