#ifndef T3_SATURN_MOVE_PROBE_H
#define T3_SATURN_MOVE_PROBE_H
#include "fight.h"
#include "psx_pose.h"
#include "saturn/core.h"

/* Optional PS1 DIVMOT logic interpreter, isolated from combat gameplay.
 * Only source files emitted into the private build/ tree may be loaded. */
sat_result_t t3_move_probe_init(void);
void t3_move_probe_reset(unsigned round);
void t3_move_probe_tick(const t3_match_t* match);
uint8_t t3_move_probe_active(void);
uint16_t t3_move_probe_slot(unsigned fighter);
uint16_t t3_move_probe_frame(unsigned fighter);
uint16_t t3_move_probe_attack_word(unsigned fighter);
uint32_t t3_move_probe_transitions(unsigned fighter);
uint8_t t3_move_probe_segments(unsigned fighter);
uint32_t t3_move_probe_decode_errors(unsigned fighter);
/* 0 no stream; 1 full pose; 2 XYZ root only;
 * 3 missing PS1 spline tables; 4 malformed/unsupported stream. */
uint8_t t3_move_probe_pose_status(unsigned fighter);
uint8_t t3_move_probe_pose_frames(unsigned fighter);
int16_t t3_move_probe_root_pose(unsigned fighter,unsigned axis);
uint32_t t3_move_probe_fk_mask(unsigned fighter);
uint32_t t3_move_probe_fk_parts(unsigned fighter);
uint32_t t3_move_probe_ik_mask(unsigned fighter);
/* Model-local PS1 defensive geometry only; not authoritative world combat. */
uint8_t t3_move_probe_hurt_ready(unsigned fighter);
int32_t t3_move_probe_hurt_head_y(unsigned fighter);
/* Atomically hand a complete PRIVATE 18-part pose to the skin preview.
 * 0 means no reliable pose; caller keeps its normal renderer. */
int t3_move_probe_copy_world(unsigned fighter,
                             t3_psx_joint_t out[T3_PSX_DRAW_PARTS],
                             uint32_t* parts_mask);
int16_t t3_move_probe_fk_root_xx(unsigned fighter);
#endif
