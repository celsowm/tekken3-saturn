#ifndef T3_GAME_ARCADE_LADDER_H
#define T3_GAME_ARCADE_LADDER_H
/* The arcade ladder (ArcadeBuildLadder, 0x800B2494) and its continue countdown.
 * Rules from Tekken3Remake docs/research/code/modes.md#arcade-ladder and #match-flow.
 * PolyForm Noncommercial 1.0.0, NOTICE.md. Required Notice: Copyright (c) 2026 n0madic. */
#include <stdint.h>

enum {
    T3_LADDER_FIGHTS=10, T3_LADDER_JIN=9, T3_LADDER_HEIHACHI=13, T3_LADDER_OGRE=14, T3_LADDER_TRUE_OGRE=20,
    /* Every fighter on the select screen: ids 0-20. */
    T3_LADDER_ROSTER=0x1FFFFF,
    /* CONTINUE? 9 .. 1: 808 fields, each digit 90 of them, a face button takes one off. */
    T3_CONTINUE_FIELDS=808, T3_CONTINUE_DIGIT=90, T3_GAME_OVER_FIELDS=121
};

/* The ten opponents of a run for the player's character `player`, drawn with the
 * x*5+1 generator from `seed` out of `roster` (a bit per selectable character id):
 * fights 1-4 from the ten original characters, 5-8 from everyone else but the
 * bosses, 9 Heihachi (Jin for a Heihachi player), 10 Ogre. */
void t3_arcade_ladder(uint32_t seed,unsigned player,uint32_t roster,uint8_t out[T3_LADDER_FIGHTS]);

/* The CPU level of ladder fight `fight` (0-9). */
static inline uint8_t t3_arcade_level(unsigned fight) {return (uint8_t)(fight<T3_LADDER_FIGHTS?fight:T3_LADDER_FIGHTS-1u);}

/* The digit CONTINUE? shows with `left` fields to go (9 for the first 88 fields). */
static inline unsigned t3_continue_digit(unsigned left) {return (left+T3_CONTINUE_DIGIT)/T3_CONTINUE_DIGIT;}

#endif
