#include "tick_clock.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

int main(void) {
    t3_tick_clock_t c={0};
    assert(t3_tick_clock_advance(&c,10,60,60)==4);
    assert(c.observed_fields==10 && c.discarded_fields==6);
    for(unsigned i=0;i<60;++i)assert(t3_tick_clock_advance(&c,1,60,60)==1);
    assert(c.observed_fields==70 && c.scheduled_ticks==64 && c.discarded_fields==6);
    c=(t3_tick_clock_t){0};
    for(unsigned i=0;i<10;++i)assert(t3_tick_clock_advance(&c,6,60,60)==4);
    assert(c.observed_fields==60 && c.scheduled_ticks==40 && c.discarded_fields==20);
    c=(t3_tick_clock_t){0};
    for(unsigned i=0;i<50;++i)(void)t3_tick_clock_advance(&c,1,50,60);
    assert(c.scheduled_ticks==60 && c.remainder==0 && !c.discarded_fields);
    assert(t3_tick_clock_advance(&c,0,50,60)==0);
    return 0;
}
