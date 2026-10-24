#ifndef T3_ATTACK_RECORDS_H
#define T3_ATTACK_RECORDS_H

#include <stdint.h>
#include "divmot.h"

/* Original PS1 move-bank section 9, decoded without allocation or transforms.
 * Coordinates and joint IDs remain in the PlayStation fighter coordinate
 * system. The presentation/physics layer must apply separate model transforms.
 */
typedef struct {
    int16_t x, y, z;
} t3_attack_point_t;

typedef struct {
    t3_attack_point_t start, end;
    /* Swept start: first active frame uses the previous arena anchor with
     * current heading/facing. Later frames copy the previous WORLD endpoint.
     * See psx_exact_contact.h for the original placement arithmetic. */
    uint8_t previous_start;
} t3_attack_segment_t;

typedef struct {
    uint8_t joint_a[2], joint_b[2];
    uint8_t segment_count, layout_kind;
    uint8_t alternative_selected;
    t3_attack_segment_t segments[2];
} t3_attack_sample_t;

/* The built-in descriptor window from 0x80017BDC: the 13 (id, offset) pairs
 * at T3_BUILTIN_ATTACK_TABLE, with pointers rewritten as window offsets by
 * extract_psx_pose_tables.py. A built-in descriptor is four joint bytes; the
 * game reads its layout word and points from the bytes after it. */
#define T3_BUILTIN_ATTACK_BYTES 0x400u
#define T3_BUILTIN_ATTACK_TABLE 0x34u
#define T3_BUILTIN_ATTACK_COUNT 13u

/* Attack segment count (+0xB4) set when a move starts: 2 when the
 * descriptor's second joint pair is set, 1 for the first, 0 without an
 * active window. Returns -1 for a descriptor that cannot be read. */
int t3_attack_joint_pairs(const t3_divmot_t* bank, const t3_move_record_t* move);

/* Returns 1 = decoded active frame; 0 = inactive/no authored geometry
 * (also built-in descriptors when the bank has no builtin_attacks window);
 * -1 = truncated, unsupported or inconsistent input.
 * choose_alternative=1 selects a valid Ogre/Mokujin alternative block.
 */
int t3_attack_sample(const t3_divmot_t* bank, const t3_move_record_t* move,
                     unsigned pose_frame, int choose_alternative,
                     t3_attack_sample_t* result);

#endif
