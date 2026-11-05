#ifndef T3_SATURN_FIGHTER_HANDS_H
#define T3_SATURN_FIGHTER_HANDS_H
/* Hand and jaw vertex variants (docs/FIGHTER_FIDELITY.md): the pack's variant
 * rows rewrite the moved vertices of the original projection's blocks and the
 * joint-local positions of the skin records they feed. */
#include <stdint.h>
#include "saturn/skin3d.h"

/* Checks that the actor's skin and projected blocks hold the pack's base
 * positions; variants stay off for the match otherwise. Returns 1 when on. */
int t3_fighter_hands_load(unsigned actor,sat_skin_asset_t* skin);
/* Shows variant `variant[channel]` on each row whose shown one differs. Call
 * on the CPU that skins the actor, before it does; returns 1 when anything
 * changed (another CPU's cached copy of the skin records is then stale). */
int t3_fighter_hands_apply(unsigned actor,sat_skin_asset_t* skin,const uint8_t variant[2]);
/* On the other CPU after an apply that changed something: drops its cached
 * copies of every row's skin records and block positions. */
sat_result_t t3_fighter_hands_sync(unsigned actor,sat_skin_asset_t* skin);
#endif
