#include "move_clock.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
int main(void) {
    t3_move_clock_t c,old;
    t3_move_clock_reset(&c,12);
    c.move_changed=1;c.hit_freeze=3;
    assert(t3_move_clock_advance(&c,0)==0);
    assert(c.pose_frame==12 && c.root_frame==12 && c.move_frame==13);
    assert(c.move_changed==0 && c.hit_freeze==2);
    assert(t3_move_clock_advance(&c,0)==0);
    assert(t3_move_clock_advance(&c,0)==1);
    assert(c.pose_frame==13 && c.root_frame==13 && c.move_frame==15);
    c.hold_frames=2;
    assert(t3_move_clock_advance(&c,13)==0);
    assert(c.pose_frame==13 && c.root_frame==14 && c.hold_frames==1);
    assert(t3_move_clock_advance(&c,13)==0);
    assert(t3_move_clock_advance(&c,13)==1);
    assert(c.pose_frame==14 && c.root_frame==14 && c.hold_frames==0);
    t3_move_clock_reset(&c,1);c.frame_step=-1;
    assert(t3_move_clock_advance(&c,0)==0 && c.pose_frame==1);
    c.ballistic=1;c.pose_frame=7;c.root_frame=6;
    assert(t3_move_clock_advance(&c,7)==0 && c.pose_frame==7 && c.root_frame==6);
    c.move_frame=32767;
    assert(t3_move_clock_advance(&c,0)==0 && c.move_frame==-32768);
    c.ballistic=2;old=c;
    assert(t3_move_clock_advance(&c,0)==-1 && !memcmp(&c,&old,sizeof(c)));
    return 0;
}
