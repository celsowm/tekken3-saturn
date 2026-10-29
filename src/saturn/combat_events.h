#ifndef T3_SATURN_COMBAT_EVENTS_H
#define T3_SATURN_COMBAT_EVENTS_H

#include <stdint.h>

/* What a fighter received or did on one simulation tick, from the owned
 * match's registered-hit and registered-block counters (psx_match: +16/+17)
 * and its attack window (+30). */
enum { T3_EVENT_HIT = 1u, T3_EVENT_BLOCK = 2u, T3_EVENT_SWING = 4u };

typedef struct {
    uint32_t tick;      /* match tick that produced it */
    uint8_t fighter;    /* receiver (HIT/BLOCK) or attacker (SWING) */
    uint8_t kind;       /* one T3_EVENT_*; a block outranks a hit on one tick */
    uint8_t joint;      /* attacker's descriptor joint (owned field 26) */
    int32_t at[3];      /* attacker's hitting segment end (fields 27-29) */
} t3_combat_event_t;

/* Producer: call once after every simulation tick, on whichever CPU ran it.
 * Events go to a bounded ring; when it is full the newest are dropped and
 * counted. Consumer: pop from the displaying CPU while no simulation job is
 * in flight; events are delivered once, in tick order. */
void t3_combat_events_tick(uint32_t tick);
int t3_combat_events_pop(t3_combat_event_t* out);
uint32_t t3_combat_events_overflow(void);
void t3_combat_events_reset(void);

#endif
