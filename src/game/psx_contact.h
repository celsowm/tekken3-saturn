#ifndef T3_PSX_CONTACT_H
#define T3_PSX_CONTACT_H
#include <stdint.h>
#include "psx_pose.h"
#include "psx_collision.h"

/* The original PS1 engine exposes 24 joint blocks, but its 14 defensive
 * cylinders refer ONLY to 18 original joint slots (indices 0..17).
 * The extra blocks 18..23 are attachment/projectile point storage, not
 * legitimate substitutes for damaged limb poses. */
enum { T3_PSX_BODY_PART_MASK=0x3FFFFu };
typedef struct {
    uint16_t move_slot, last_frame;
    uint8_t segment_count, valid;
    t3_world_segment_t previous[2];
} t3_psx_sweep_history_t;

typedef struct {
    int8_t zone;          /* -1 = no hit */
    uint8_t segment;      /* original authored segment 0 or 1 */
    uint8_t evaluated;    /* 1 only after complete geometry validation */
} t3_psx_contact_result_t;

/* Build original 14 hurt cylinders from original joint-slot model/world-space
 * part translations. All referenced parts MUST be marked valid.
 * Neither the 18-part array nor the resulting zones imply calibrated world
 * root placement: the caller must supply parts in the SAME space as attacks.
 * Returns 1 if all body joints are known, 0 otherwise (no output changes). */
int t3_psx_hurt_from_parts(const t3_psx_joint_t parts[T3_PSX_DRAW_PARTS],
                           uint32_t valid_mask, int large,
                           t3_hurt_cylinder_t out[T3_PSX_HURT_ZONES]);

/* The original attack layouts 1,2,4,6,7 reuse the exact previous world
 * endpoint of segment 0; layouts 2,3,9 do so for segment 1.
 * This cannot be reconstructed accurately by re-rotating old local points.
 *
 * frame = authored move-relative frame in the active attack window (0 first)
 * move_slot identifies the currently active move and prevents stale segments
 * from being reused across move switches. Previous frame continuity is
 * mandatory for swept layouts. There is no heap or 2D approximation.
 *
 *  1 complete valid authored world segments and updated history
 *  0 missing prior-frame data/root; no changes to output or history
 * -1 unsupported or malformed arguments; no changes
 */
int t3_psx_sweep_sample(const t3_attack_sample_t* sample,
                        uint16_t move_slot, unsigned frame,
                        const t3_psx_root_t* current,
                        const t3_psx_root_t* previous_root,
                        t3_psx_sweep_history_t* history,
                        t3_world_attack_t* out);

/* Contact geometry only. Does NOT apply Tekken damage, guard, throw, hit
 * cooldown, juggling, combo or posture rules; those belong to HitTest and
 * HitApply. Returns 1 known contact status, 0 malformed/unknown geometry.
 * On success, result.zone==-1 means a verified miss. */
int t3_psx_contact_probe(const t3_world_attack_t* attack,
                         const t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES],
                         t3_psx_contact_result_t* result);

#endif
