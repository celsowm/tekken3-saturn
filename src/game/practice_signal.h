/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Practice's FREEZE SIGNAL (practice.ovl FUN_800B3520, FUN_800B6CF4), adapted from Tekken3Remake
 * remake/core/fighter/move_system.gd (can_act, _can_act_find) and modes.md#practice; see NOTICE.md. */
#ifndef T3_PRACTICE_SIGNAL_H
#define T3_PRACTICE_SIGNAL_H
#include <stdint.h>
#include "divmot.h"
#include "branch_rules.h"

/* A branch of the running move could be taken now (input aside), or, near the move's end, a branch of its default
 * continuation (the END row's target). `bank` and `common` are the fighter's own banks; `current` is the running
 * move and `frame` its pose frame; `frame_step_positive` is the animation playing forwards. */
int t3_practice_can_act(const t3_divmot_t* bank,const t3_divmot_t* common,const t3_move_record_t* current,
                        unsigned frame,int frame_step_positive,uint8_t own_bank,const t3_branch_context_t* facts);
/* The back light colour the signal gives a fighter (FUN_800B6CF4): green while it can act, red while it cannot. */
void t3_practice_signal_colour(int can_act,uint8_t rgb[3]);
#endif
