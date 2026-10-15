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
    /* 1 -> start was a point from the preceding pose frame, which needs the
     * PREVIOUS frame's root transform for accurate swept collisions. */
    uint8_t previous_start;
} t3_attack_segment_t;

typedef struct {
    uint8_t joint_a[2], joint_b[2];
    uint8_t segment_count, layout_kind;
    uint8_t alternative_selected;
    t3_attack_segment_t segments[2];
} t3_attack_sample_t;

/* Returns 1 = decoded active frame; 0 = inactive/no authored geometry
 * (also includes 13 built-in descriptors, not reconstructed yet);
 * -1 = truncated, unsupported or inconsistent input.
 * choose_alternative=1 selects a valid Ogre/Mokujin alternative block.
 */
int t3_attack_sample(const t3_divmot_t* bank, const t3_move_record_t* move,
                     unsigned pose_frame, int choose_alternative,
                     t3_attack_sample_t* result);

#endif
