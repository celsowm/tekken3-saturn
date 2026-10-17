/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Result normalization/classification and VS RoundWins/MatchResult adapted
 * from pinned Tekken3Remake round_flow.gd. Changes: typed bounded state; no
 * intro/replay/win-pose state machine or original round-flow parity claim.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "psx_round.h"
static unsigned tie_breaker(const t3_psx_round_score_t* s) {
    return s->human_mask==1u?1u:s->human_mask==3u?s->tie_choice:0u;
}
int t3_psx_round_award(t3_psx_round_score_t* score,uint16_t a,uint16_t b) {
    t3_psx_round_score_t n;
    if(!score || a>4096u || b>4096u || !score->to_win || score->to_win>5u ||
       !score->played || score->played>9u || score->max_rounds<1 || score->max_rounds>9 ||
       score->wins[0]>5u || score->wins[1]>5u || score->human_mask>3u ||
       score->tie_choice>1u || score->draw_wins>1u)return 0;
    n=*score;
    n.round_won[0]=n.round_won[1]=-1;
    if(a!=b) {
        unsigned winner=a>b?0u:1u;
        n.round_won[winner]=1;++n.wins[winner];
    } else {
        unsigned first=0,second;
        --n.max_rounds;
        if(n.wins[1]>n.wins[0] || (n.wins[0]==n.wins[1] && tie_breaker(&n)))first=1;
        second=1u-first;++n.wins[first];++n.wins[second];
        if(!n.draw_wins) {
            if(n.wins[first]>=n.to_win)n.round_won[first]=1;
            if(n.wins[second]>=n.to_win)--n.wins[second];
        } else if(n.wins[first]>=n.to_win && n.wins[second]<n.to_win)n.round_won[first]=1;
    }
    if(n.max_rounds<=(int)n.played || n.wins[0]>=n.to_win || n.wins[1]>=n.to_win) {
        unsigned winner=n.wins[0]==n.wins[1]?tie_breaker(&n):n.wins[0]>n.wins[1]?0u:1u;
        n.match_result=n.wins[0]==n.wins[1]?3u:(uint8_t)(winner+1u);
        n.match_winner=(int8_t)winner;n.match_loser=(int8_t)(1u-winner);
    }
    *score=n;return 1;
}
void t3_psx_round_init(t3_psx_round_t* r) {
    *r=(t3_psx_round_t){.timer=2400,.number=1,.winner=-1,.max_rounds=3};
}
int t3_psx_round_step(t3_psx_round_t* r,const int32_t hp[2],int32_t maximum) {
    if(r->done) {
        if(r->hold)--r->hold;
        if(r->hold || r->match_done)return 0;
        ++r->number;r->timer=2400;r->done=0;r->winner=-1;r->result_flags=0;
        return 1;
    }
    if(hp[0]>0 && hp[1]>0 && r->timer) {--r->timer;return 0;}
    /* FUN_8003E6D0: ceil(health/(maximum >> 12)), not a host float ratio. */
    int32_t unit=maximum>>12;
    int32_t a=unit?(int32_t)(((int64_t)hp[0]+unit-1)/unit):0;
    int32_t b=unit?(int32_t)(((int64_t)hp[1]+unit-1)/unit):0;
    uint16_t time=r->timer?0u:1u;
    r->result_flags=time;
    if(a==b)r->result_flags=(uint16_t)(time|(a?0x20u:0x26u));
    else if(!a || !b) {
        int32_t winner=a>b?a:b;
        r->result_flags=(uint16_t)(time|(winner==0x1000?10u:winner<0xCD?0x12u:2u));
    }
    t3_psx_round_score_t score={0};
    score.wins[0]=r->wins[0];score.wins[1]=r->wins[1];
    score.played=r->number;score.to_win=2;score.max_rounds=r->max_rounds;
    score.human_mask=3;score.draw_wins=1; /* Observed two-human VS defaults, tie choice 0. */
    if(!t3_psx_round_award(&score,(uint16_t)a,(uint16_t)b))return 0;
    r->winner=score.match_result==3?-1:score.match_result?score.match_winner:score.round_won[0]>0?0:score.round_won[1]>0?1:-1;
    r->wins[0]=(uint8_t)score.wins[0];r->wins[1]=(uint8_t)score.wins[1];
    r->max_rounds=(uint8_t)score.max_rounds;r->match_done=score.match_result!=0;
    r->done=1;r->hold=time?90u:240u;
    return 0;
}
