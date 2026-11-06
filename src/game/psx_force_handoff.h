#ifndef T3_PSX_FORCE_HANDOFF_H
#define T3_PSX_FORCE_HANDOFF_H
#include <stdint.h>

/* What the Tekken Force programs hand each other through sat_exec (docs/TEKKEN_FORCE_PLAN.md, decision 1): a
 * level is the scrolling section, then the boss duel in the versus program, then back to the next level. The
 * block is not the 16-byte boot argument of the other programs, so a program that receives the other size
 * treats the start as a cold boot. Big-endian, T3_FORCE_HANDOFF_BYTES long (at most SAT_EXEC_ARG_MAX, 64). */
#define T3_FORCE_HANDOFF_BYTES 40u
#define T3_FORCE_HANDOFF_VERSION 1u

enum { T3_FORCE_SECTION_SCROLL=0, T3_FORCE_SECTION_BOSS=1, T3_FORCE_SECTION_RESULT=2 };
enum { T3_FORCE_FLAG_KEY_EARNED=1, T3_FORCE_FLAG_FINAL=2, T3_FORCE_FLAG_DOCTOR_B=4, T3_FORCE_FLAG_BOSS_WON=8 };

typedef struct {
    uint8_t level;        /* 0..3 */
    uint8_t section;      /* T3_FORCE_SECTION_* */
    uint8_t character;    /* the player's character */
    uint8_t costume;      /* its costume */
    uint8_t keys;         /* keys held (0..3) */
    uint8_t flags;        /* T3_FORCE_FLAG_* */
    uint32_t score,hiscore;
    int32_t timer;        /* frames left (the original's 0x800AE094) */
    int32_t health;       /* the player's health (16.16) */
    uint16_t kills;
    uint32_t seed;        /* the fight's random seed */
    uint8_t pad_side;     /* the human's pad */
} t3_force_handoff_t;

/* Packs into `out` (T3_FORCE_HANDOFF_BYTES) and unpacks; unpack returns 0 for a block that is not a Force
 * hand-off (size, magic, version, or a field out of range). */
void t3_force_handoff_pack(const t3_force_handoff_t* in,uint8_t* out);
int t3_force_handoff_unpack(const uint8_t* in,uint32_t bytes,t3_force_handoff_t* out);
#endif
