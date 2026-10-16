#include "psx_round.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
int main(void) {
    t3_psx_round_t r;int32_t hp[2]={140*65536,0};
    t3_psx_round_init(&r);
    assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(r.done && r.winner==0 && r.wins[0]==1 && r.result_flags==10 && r.hold==240);
    for(unsigned i=0;i<239;++i)assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(t3_psx_round_step(&r,hp,140*65536));
    assert(r.number==2 && r.timer==3600 && !r.done && r.wins[0]==1);
    assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(r.match_done && r.wins[0]==2);
    for(unsigned i=0;i<500;++i)assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(r.wins[0]==2 && r.number==2);
    t3_psx_round_init(&r);hp[0]=hp[1]=140*65536;
    for(unsigned i=0;i<3600;++i)assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(!r.done && !r.timer);
    assert(!t3_psx_round_step(&r,hp,140*65536));
    assert(r.done && r.result_flags==0x21 && r.winner==-1 && !r.wins[0] && !r.wins[1] && r.hold==90);
    t3_psx_round_init(&r);hp[0]=hp[1]=0;
    t3_psx_round_step(&r,hp,140*65536);assert(r.result_flags==0x26 && r.winner==-1);
    t3_psx_round_init(&r);hp[0]=65536;hp[1]=0;
    t3_psx_round_step(&r,hp,140*65536);assert(r.result_flags==0x12);
    t3_psx_round_init(&r);hp[0]=70*65536;hp[1]=0;
    t3_psx_round_step(&r,hp,140*65536);assert(r.result_flags==2);
    return 0;
}
