/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of ArcadeBuildLadder (0x800B2494, arcade.ovl) and the frame
 * generator FUN_8004D13C from pinned Tekken3Remake docs/research/code/modes.md.
 * Changes: the seed is the caller's (the original's generator also advances
 * elsewhere), every fighter counts as unlocked, and costumes are not chosen
 * because the port has one per character. */
#include "arcade_ladder.h"

enum { ORIGINAL_TEN=0x3FF, BOSSES=(1u<<T3_LADDER_HEIHACHI)|(1u<<T3_LADDER_OGRE)|(1u<<T3_LADDER_TRUE_OGRE), IDS=21 };

/* Draws one of the pool's first `*count` ids and moves the last into its place. */
static uint8_t draw(uint32_t* rng,uint8_t pool[IDS],unsigned* count) {
    *rng=*rng*5u+1u;
    const unsigned i=*rng%*count;
    const uint8_t id=pool[i];
    pool[i]=pool[--*count];
    return id;
}

static unsigned ids(uint32_t mask,uint8_t pool[IDS]) {
    unsigned n=0;
    for(unsigned id=0;id<IDS;++id)if(mask>>id&1u)pool[n++]=(uint8_t)id;
    return n;
}

void t3_arcade_ladder(uint32_t seed,unsigned player,uint32_t roster,uint8_t out[T3_LADDER_FIGHTS]) {
    const unsigned boss=player==T3_LADDER_HEIHACHI?T3_LADDER_JIN:T3_LADDER_HEIHACHI;
    uint32_t left=roster&~BOSSES&~(1u<<(player&31u))&~(1u<<boss);
    uint8_t pool[IDS];
    unsigned count=ids(left&ORIGINAL_TEN,pool),fight=0;
    for(;fight<4u;++fight) {
        out[fight]=draw(&seed,pool,&count);
        left&=~(1u<<out[fight]);
    }
    count=ids(left,pool);
    for(;fight<8u;++fight)out[fight]=draw(&seed,pool,&count);
    out[8]=(uint8_t)boss;
    out[9]=T3_LADDER_OGRE;
}
