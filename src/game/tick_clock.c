#include "tick_clock.h"

uint32_t t3_tick_clock_advance(t3_tick_clock_t* c,uint32_t elapsed,
                               uint32_t refresh_hz,uint32_t logic_hz) {
    c->elapsed_last=elapsed;
    c->observed_fields+=elapsed;
    if(elapsed>T3_MAX_RECOVERY_FIELDS) {
        c->discarded_fields+=elapsed-T3_MAX_RECOVERY_FIELDS;
        elapsed=T3_MAX_RECOVERY_FIELDS;
    }
    c->remainder+=elapsed*logic_hz;
    uint32_t ticks=c->remainder/refresh_hz;
    c->remainder%=refresh_hz;
    c->scheduled_ticks+=ticks;
    return ticks;
}
