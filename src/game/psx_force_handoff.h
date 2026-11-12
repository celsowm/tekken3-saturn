#ifndef T3_PSX_FORCE_HANDOFF_H
#define T3_PSX_FORCE_HANDOFF_H
#include <stdint.h>

/* Force stage continuation through sat_exec. Boss swaps happen within the
 * current program; a clear carries this state to the next scrolling stage
 * or the earned Doctor B. stage. Big-endian, distinct from the menu's boot
 * argument. Version 2 also retains the encountered bosses and record flag. */
#define T3_FORCE_HANDOFF_BYTES 40u
#define T3_FORCE_HANDOFF_VERSION 2u

enum { T3_FORCE_SECTION_SCROLL=0, T3_FORCE_SECTION_BOSS=1, T3_FORCE_SECTION_RESULT=2 };
enum { T3_FORCE_FLAG_KEYS=7, T3_FORCE_FLAG_SAVED_DOCTOR=8, T3_FORCE_FLAG_DOCTOR_UNLOCKED=16 };

typedef struct {
    uint8_t level;        /* 0..3 scrolling stages; 4 is the earned Doctor B. fight */
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
    uint8_t bosses[4];    /* original character*4+costume; 0xFF when not met */
    uint8_t new_record;
} t3_force_handoff_t;

/* Packs into `out` (T3_FORCE_HANDOFF_BYTES) and unpacks; unpack returns 0 for a block that is not a Force
 * hand-off (size, magic, version, or a field out of range). */
void t3_force_handoff_pack(const t3_force_handoff_t* in,uint8_t* out);
int t3_force_handoff_unpack(const uint8_t* in,uint32_t bytes,t3_force_handoff_t* out);
#endif
