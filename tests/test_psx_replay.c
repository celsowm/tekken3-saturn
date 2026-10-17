#include "psx_replay.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
static t3_psx_replay_storage_t storage;
_Static_assert(sizeof(t3_psx_replay_frame_t)==48,"portable replay frame budget");
_Static_assert(sizeof(t3_psx_replay_pair_t)==100,"portable replay pair budget");
int main(void) {
    t3_psx_replay_t s,before;
    t3_psx_replay_reset(&s,7);
    assert(s.mode==7 && s.mark==-2 && !s.recorded && s.epoch==7);
    s.record=299;assert(t3_psx_replay_update(&s)==0 && !s.record && s.recorded==1);
    s.recorded=0x70000000;assert(t3_psx_replay_update(&s)==0 && s.recorded==0x70000000);
    s.record=300;before=s;assert(t3_psx_replay_update(&s)==-1 && !memcmp(&s,&before,sizeof(s)));
    t3_psx_replay_reset(&s,7);
    assert(t3_psx_replay_start(&s) && s.mode==5 && s.mark==280);
    assert(!t3_psx_replay_update(&s));
    t3_psx_replay_reset(&s,7);s.record=10;s.recorded=500;
    assert(t3_psx_replay_mark_ko(&s,23) && s.mark==514);
    for(unsigned i=0;i<90;++i)assert(!t3_psx_replay_update(&s));
    assert(s.record==100 && t3_psx_replay_start(&s) && s.play==500 && s.mode==2);
    unsigned frames=0;
    while(t3_psx_replay_update(&s)>0){assert(++frames<=300);}
    assert(frames==300 && s.mode==5 && s.play==198);
    t3_psx_replay_reset(&s,7);s.record=100;s.recorded=100;s.mark=50;
    assert(t3_psx_replay_start(&s) && s.play==50); /* short history never invents 150 recorded frames */
    assert(t3_psx_replay_update(&s)==1 && s.mode==3);
    assert(t3_psx_replay_update(&s)==1 && s.mode==1);
    s.skip=1;assert(t3_psx_replay_update(&s)==1 && s.mode==4 && s.play==51);
    assert(!t3_psx_replay_update(&s) && s.mode==5);
    storage.epoch=7;storage.ring[1].epoch=7;storage.ring[1].actor[0].slot=357;
    assert(t3_psx_replay_at(&storage,&s,1)->actor[0].slot==357);
    ++s.epoch;assert(!t3_psx_replay_at(&storage,&s,1));
    assert(!t3_psx_replay_at(0,&s,1) && !t3_psx_replay_at(&storage,0,1));
    assert(!t3_psx_replay_at(&storage,&s,300));
    t3_vec3i_t a={-3,-1,2},b={0,0,3},root=t3_psx_replay_root(a,b,1);
    assert(root.x==-1 && root.y==0 && root.z==2);
    root=t3_psx_replay_root(a,b,0);assert(!memcmp(&root,&a,sizeof(a)));
    before=s;assert(!t3_psx_replay_mark_ko(&s,32768) && !memcmp(&s,&before,sizeof(s)));
    return 0;
}
