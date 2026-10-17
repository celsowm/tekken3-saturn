/* Bulk host adapter. Each input line is one RoundFlow entry observation,
 * not a continuous replay. No original bytes are embedded in the executable. */
#include "psx_round_flow.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define GLOBALS(X) \
 X(state,state) X(counter,counter) X(hold,hold) X(done,done) \
 X(replay_request,replay_request) X(frame,frame) X(played,played) X(timer,timer) \
 X(result,result) X(input0,input[0]) X(input1,input[1]) X(freeze,freeze) \
 X(no_damage,no_damage) X(camera,camera) X(timer_stopped,timer_stopped) \
 X(to_win,to_win) X(max_rounds,max_rounds) X(blend_hold,blend_hold) \
 X(human_mask,human_mask) X(tie_choice,tie_choice) X(draw_wins,draw_wins) \
 X(match_result,match_result) X(match_winner,match_winner) X(match_loser,match_loser) \
 X(win_done0,win_done[0]) X(win_done1,win_done[1])
#define ACTOR(X) \
 X(health) X(maximum) X(wins) X(won) X(win_pose) X(active) X(pose) X(length) \
 X(bank_type) X(state_word) X(script_input) X(is_cpu) X(costume) X(health_left) X(carried_health)
static int value(char** cursor,int32_t* out) {
    char* end;long n;
    errno=0;n=strtol(*cursor,&end,10);
    if(errno || end==*cursor || n<INT32_MIN || n>INT32_MAX)return 0;
    *cursor=end;*out=(int32_t)n;return 1;
}
int main(void) {
    char line[4096];
    while(fgets(line,sizeof(line),stdin)) {
        char* p=line;
        t3_psx_round_flow_t s={0};t3_psx_round_actor_t a[2]={{0}};
        t3_psx_round_flow_facts_t f={0};t3_psx_round_flow_actions_t x;
        int32_t n;
        unsigned first=1;
#define READ_GLOBAL(name,member) if(!value(&p,&s.member))return 2;
        GLOBALS(READ_GLOBAL)
#undef READ_GLOBAL
        for(unsigned i=0;i<2;++i) {
#define READ_ACTOR(name) if(!value(&p,&a[i].name))return 2;
            ACTOR(READ_ACTOR)
#undef READ_ACTOR
        }
        if(!value(&p,&f.replay_mode) || !value(&p,&f.replay_skip) || !value(&p,&n) || n<0)return 2;
        f.frame_counter=(uint32_t)n;
        for(unsigned i=0;i<2;++i){if(!value(&p,&n) || n<0 || n>65535)return 2;f.physical[i]=(uint16_t)n;}
        for(unsigned i=0;i<2;++i){if(!value(&p,&n) || n<0 || n>65535)return 2;f.pressed[i]=(uint16_t)n;}
        while(*p==' ' || *p=='\t' || *p=='\r' || *p=='\n')++p;
        if(*p || !t3_psx_round_flow_step(&s,a,&f,&x))return 2;
        printf("{\"round\":{");
#define PRINT_GLOBAL(name,member) printf("%s\"" #name "\":%d",first?"":",",s.member);first=0;
        GLOBALS(PRINT_GLOBAL)
#undef PRINT_GLOBAL
        printf("},\"fighters\":[");
        for(unsigned i=0;i<2;++i) {
            printf("%s{",i?",":"");
#define PRINT_ACTOR(name) printf("\"" #name "\":%d,",a[i].name);
            ACTOR(PRINT_ACTOR)
#undef PRINT_ACTOR
            printf("\"start_slot\":%d}",x.start_slot[i]);
        }
        printf("],\"clear_blends\":%u,\"grounded_mask\":%u,\"animate_before_camera\":%u,\"fight_clock_advance\":%u}\n",
               x.clear_blends,x.grounded_mask,x.animate_before_camera,x.fight_clock_advance);
    }
    return ferror(stdin)?1:0;
}
