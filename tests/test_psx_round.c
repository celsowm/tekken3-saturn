#include "psx_round.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
static t3_psx_round_score_t settings(void) {
    t3_psx_round_score_t s={0};s.played=1;s.to_win=2;s.max_rounds=3;s.human_mask=3;return s;
}
static void original_vs_scoring(void) {
    t3_psx_round_score_t s=settings(),before;
    s.match_winner=1;s.match_loser=0;
    assert(t3_psx_round_award(&s,4096,2048));
    assert(s.wins[0]==1 && !s.wins[1] && s.round_won[0]==1 && s.round_won[1]==-1 && !s.match_result);
    assert(s.match_winner==1 && s.match_loser==0);
    s.played=2;assert(t3_psx_round_award(&s,4096,0));
    assert(s.wins[0]==2 && s.match_result==1 && s.match_winner==0 && s.match_loser==1);
    for(unsigned tie=0;tie<2;++tie) {
        s=settings();s.tie_choice=(uint8_t)tie;
        assert(t3_psx_round_award(&s,4096,4096));
        assert(s.max_rounds==2 && s.wins[0]==1 && s.wins[1]==1 && !s.match_result);
        assert(s.round_won[0]==-1 && s.round_won[1]==-1);
        s.played=2;assert(t3_psx_round_award(&s,0,0));
        assert(s.max_rounds==1 && s.wins[tie]==2 && s.wins[1-tie]==1);
        assert(s.match_result==tie+1 && s.match_winner==(int)tie && s.round_won[tie]==1);
    }
    for(unsigned mask=0;mask<4;++mask) {
        s=settings();s.wins[0]=s.wins[1]=1;s.played=2;s.draw_wins=1;
        s.human_mask=(uint8_t)mask;s.tie_choice=1;
        assert(t3_psx_round_award(&s,4096,4096));
        assert(s.wins[0]==2 && s.wins[1]==2 && s.match_result==3);
        assert(s.match_winner==(mask==1 || mask==3?1:0));
        assert(s.round_won[0]==-1 && s.round_won[1]==-1);
    }
    s=settings();s.wins[1]=1;assert(t3_psx_round_award(&s,2000,2000));
    assert(s.wins[0]==1 && s.wins[1]==2 && s.match_result==2);
    s=settings();s.max_rounds=1;s.wins[1]=1;
    assert(t3_psx_round_award(&s,4096,0));assert(s.match_result==3 && s.match_winner==0);
    s=settings();before=s;assert(!t3_psx_round_award(&s,4097,0));assert(!memcmp(&s,&before,sizeof(s)));
    s.to_win=0;before=s;assert(!t3_psx_round_award(&s,0,0));assert(!memcmp(&s,&before,sizeof(s)));
}
int main(void) {
    original_vs_scoring();
    t3_psx_round_t r;int32_t hp[2]={140*65536,0};
    t3_psx_round_init(&r);
    assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(r.done && r.winner==0 && r.wins[0]==1 && r.result_flags==10 && r.hold==240);
    for(unsigned i=0;i<239;++i)assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(t3_psx_round_step(&r,hp,140*65536));
    assert(r.number==2 && r.timer==2400 && !r.done && r.wins[0]==1);
    assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(r.match_done && r.wins[0]==2);
    for(unsigned i=0;i<500;++i)assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(r.wins[0]==2 && r.number==2);
    t3_psx_round_init(&r);hp[0]=hp[1]=140*65536;
    for(unsigned i=0;i<2400;++i)assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(!r.done && !r.timer);
    assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(r.done && r.result_flags==0x21 && r.winner==-1 && r.wins[0]==1 && r.wins[1]==1 && r.max_rounds==2 && r.hold==90);
    for(unsigned i=0;i<89;++i)assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(t3_psx_round_step(&r,hp,140*65536));
    r.timer=0;t3_psx_round_step(&r,hp,140*65536);
    assert(r.match_done && r.wins[0]==2 && r.wins[1]==2 && r.winner==-1);
    t3_psx_round_init(&r);hp[0]=hp[1]=0;
    t3_psx_round_step(&r,hp,140*65536);assert(r.result_flags==0x26 && r.winner==-1);
    t3_psx_round_init(&r);hp[0]=65536;hp[1]=0;
    t3_psx_round_step(&r,hp,140*65536);assert(r.result_flags==0x12);
    t3_psx_round_init(&r);hp[0]=70*65536;hp[1]=0;
    t3_psx_round_step(&r,hp,140*65536);assert(r.result_flags==2);
    return 0;
}
