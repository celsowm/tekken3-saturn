#ifndef T3_TICK_CLOCK_H
#define T3_TICK_CLOCK_H
#include <stdint.h>

enum { T3_MAX_RECOVERY_FIELDS = 4 };
/* Counters describe scheduling, including time discarded by the legacy clamp.
 * They do not establish that a scheduled tick was committed or displayed. */
typedef struct {
    uint32_t remainder, elapsed_last, observed_fields, discarded_fields, scheduled_ticks;
} t3_tick_clock_t;
uint32_t t3_tick_clock_advance(t3_tick_clock_t* clock, uint32_t elapsed,
                               uint32_t refresh_hz, uint32_t logic_hz);
#endif
