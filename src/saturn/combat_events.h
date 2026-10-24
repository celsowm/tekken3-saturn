#ifndef T3_SATURN_COMBAT_EVENTS_H
#define T3_SATURN_COMBAT_EVENTS_H

#include <stdint.h>

/* What a fighter received since the last poll, from the owned match's
 * registered-hit and registered-block counters (psx_match: +16/+17). */
enum { T3_EVENT_HIT = 1u, T3_EVENT_BLOCK = 2u, T3_EVENT_SWING = 4u, T3_EVENT_BITS = 4u };

/* Returns the events of fighters 0 and 1: bits 0-3 for fighter 0, bits 4-7 for
 * fighter 1 (shift a T3_EVENT_* by T3_EVENT_BITS * fighter). HIT and BLOCK mean
 * the fighter received one (a block takes precedence in the same poll); SWING is
 * the first image of one of its attack windows. */
uint8_t t3_combat_events_poll(void);
void t3_combat_events_reset(void);

#endif
