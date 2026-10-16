#ifndef T3_PSX_EXACT_CONTACT_H
#define T3_PSX_EXACT_CONTACT_H
#include "psx_pose.h"
/* Original integer SegmentHitsCylinder, including wrapped projection products.
 * This intentionally preserves PS1 decisions instead of ideal geometry. */
int t3_psx_segment_hits_cylinder(const t3_world_segment_t* segment,const t3_hurt_cylinder_t* cylinder);
/* Original baked-point placement. Both anchors are PS1 arena anchors; heading
 * and facing are CURRENT 16-bit angles even for the first swept start. The
 * previous segment endpoints are copied verbatim on later swept frames. */
int t3_psx_attack_place(const t3_attack_sample_t* sample,unsigned active_frame,
                         t3_vec3i_t anchor,t3_vec3i_t previous_anchor,t3_vec3i_t root,
                         uint16_t heading,uint16_t facing,const t3_psx_trig_t* trig,
                         const t3_world_attack_t* previous,t3_world_attack_t* out);
/* Exact first segment/zone decision: -1 miss, -2 invalid input. */
int t3_psx_attack_contact(const t3_world_attack_t* attack,
                           const t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES],unsigned* segment);
#endif
