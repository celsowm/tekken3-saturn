/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of the matrices Tekken Ball's ball drawing builds (volley.ovl FUN_800B3198 and its shadow) as ported
 * in the pinned Tekken3Remake (tools/research/ball_sim.py ball_draw, _shadow, projection_sim.py). Changes: plain
 * int32 matrices (entries 4.12) instead of GTE registers and guest RAM. */
#ifndef T3_BALL_POSE_H
#define T3_BALL_POSE_H
#include <stdint.h>

/* RotMatrix: the rotation of three angles (4096 a turn), `trig` the executable's sine table (10,240 bytes, the sine
 * words then the cosine words from byte 2048). */
void t3_ball_rotation(const uint8_t* trig,const int32_t angle[3],int32_t m[3][3]);
/* out = a * b (4.12); out may be a or b. */
void t3_ball_multiply(const int32_t a[3][3],const int32_t b[3][3],int32_t out[3][3]);
/* The ball's matrix: its spin, then the squash along the axis angles (axis * diag(1-q, 1+q, 1+q) * axis^T * spin). */
void t3_ball_pose(const uint8_t* trig,const int32_t spin[3],const int32_t axis[3],int32_t squash,int32_t m[3][3]);
/* The shadow's matrix: the ball's yaw (spin[1]) turned by axis * diag(1-q, 1+q, 1+q) about the axis' yaw (axis[1]). */
void t3_ball_shadow_pose(const uint8_t* trig,int32_t yaw,int32_t axis_yaw,int32_t squash,int32_t m[3][3]);
#endif
