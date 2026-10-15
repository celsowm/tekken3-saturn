#ifndef T3_PSX_ROOT_H
#define T3_PSX_ROOT_H
#include <stdint.h>
#include "psx_pose.h"

/* Equivalent to FighterSkeleton.root_displacement + RootUpdate in the PS1
 * research: KMD scale_percent /100, original s16-wrapped pose root XYZ,
 * facing controls how displacement projects into arena coordinates,
 * and heading controls the root's model rotation.
 *
 * World anchor and both angular inputs MUST be from authentic PS1 state
 * before these become combat-authoritative; the Saturn demo's Q8 x/z is
 * not a substitute. For model-local visual previews, callers may explicitly
 * use zero anchor, heading=facing=0x8000 (no arena placement).
 */
int t3_psx_pose_world_root(const int16_t pose[T3_PSX_POSE_CHANNELS],
                           unsigned scale_percent, t3_vec3i_t anchor,
                           uint16_t facing, uint16_t heading,
                           const t3_psx_trig_t* trig,
                           t3_psx_joint_t* out);

#endif
