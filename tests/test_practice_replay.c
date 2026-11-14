#include "practice_replay.h"
#include <assert.h>
int main(void) {
    t3_practice_replay_t r={0,0};
    t3_practice_replay_in_t in={.setting=T3_REPLAY_COMBO4};
    uint8_t dropped;
    /* Frames count up to 300; a combo of 4 that ends starts a replay from the combo's frames plus 30. */
    for(int i=0;i<100;++i)assert(t3_practice_replay_step(&r,&in,&dropped)==0);
    assert(r.replay_frames==100 && r.combo_frames==100);
    in.acting=1;in.move_length=40;in.combo=0;in.last=0;
    t3_practice_replay_step(&r,&in,&dropped);assert(r.combo_frames==40);              /* an attack keeps the counter at its length */
    in.acting=0;in.combo=3;in.last=3;for(int i=0;i<10;++i)t3_practice_replay_step(&r,&in,&dropped);
    in.combo=0;in.last=4;in.ended=4;                                                  /* the combo of 4 ends: dropped = 4 */
    unsigned frames=t3_practice_replay_step(&r,&in,&dropped);
    assert(dropped==4 && frames==T3_PRACTICE_REPLAY_MIN && r.replay_frames==0 && r.combo_frames==0);   /* 81 frames, raised to the 90 minimum */
    /* A longer combo plays its frames plus 30, never more than were counted. */
    t3_practice_replay_t q={0,0};t3_practice_replay_in_t qi={.setting=T3_REPLAY_COMBO4};
    for(int i=0;i<200;++i)t3_practice_replay_step(&q,&qi,&dropped);
    qi.acting=1;qi.move_length=60;t3_practice_replay_step(&q,&qi,&dropped);
    qi.acting=0;qi.combo=4;qi.last=4;for(int i=0;i<20;++i)t3_practice_replay_step(&q,&qi,&dropped);
    qi.combo=0;qi.last=5;qi.ended=5;frames=t3_practice_replay_step(&q,&qi,&dropped);
    assert(frames==60+20+1+0x1E);
    /* A combo of 3 does not start a +4 replay. */
    for(int i=0;i<100;++i)t3_practice_replay_step(&r,&in,&dropped);
    in.ended=3;assert(t3_practice_replay_step(&r,&in,&dropped)==0);
    /* MANUAL: Select after 90 frames replays everything recorded, at most 0x118 frames. */
    t3_practice_replay_t m={0,0};t3_practice_replay_in_t mi={.setting=T3_REPLAY_MANUAL};
    for(int i=0;i<88;++i)t3_practice_replay_step(&m,&mi,&dropped);
    mi.select_pressed=1;assert(t3_practice_replay_step(&m,&mi,&dropped)==0);          /* 89 frames: too short */
    mi.select_pressed=0;for(int i=0;i<250;++i)t3_practice_replay_step(&m,&mi,&dropped);
    mi.select_pressed=1;frames=t3_practice_replay_step(&m,&mi,&dropped);
    assert(frames==0x118 && m.replay_frames==0);
    /* The pause or a playing replay stops the count. */
    t3_practice_replay_t p={0,0};t3_practice_replay_in_t pi={.setting=T3_REPLAY_COMBO4,.paused=1};
    t3_practice_replay_step(&p,&pi,&dropped);assert(p.replay_frames==0);
    pi.paused=0;pi.replaying=1;t3_practice_replay_step(&p,&pi,&dropped);assert(p.replay_frames==0);
    return 0;
}
