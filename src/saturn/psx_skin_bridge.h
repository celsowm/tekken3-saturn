#ifndef T3_SATURN_PSX_SKIN_BRIDGE_H
#define T3_SATURN_PSX_SKIN_BRIDGE_H

#include <stdint.h>
#include "psx_pose.h"
#include "saturn/math3d.h"

/* PS1 KMD coordinates are expressed in integer units, where 4096 =
 * one LibSaturn unit. A Q12 matrix therefore becomes Q16 with << 4.
 * This bridge is a PRIVATE visual study, not a claim of exact orientation
 * parity: the real PS1 part basis and facing transform still need an oracle.
 *
 * The first 18 draw parts correspond to skin-visible KMD geometry.
 * The remaining 4 costume/accessory parts have no imported geometry yet.
 * All 18 must be valid; otherwise the output is left untouched so the
 * existing procedural/rest-pose renderer stays in use.
 */
#define T3_PSX_VISIBLE_PARTS 18u
#define T3_PSX_VISIBLE_MASK ((1u << T3_PSX_VISIBLE_PARTS) - 1u)

int t3_psx_skin_bridge(const t3_psx_joint_t pose[T3_PSX_DRAW_PARTS],
                       uint32_t valid_parts,
                       sat_mat4_t matrices[T3_PSX_DRAW_PARTS]);

#endif
