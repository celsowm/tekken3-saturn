/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from Tekken3Remake remake/core/game/practice_mode.gd; see practice_dummy.h and NOTICE.md. */
#include "practice_dummy.h"

/* FUN_800B8904: lying down, the dummy holds up; true when it may act. */
static int standing_ok(const t3_practice_dummy_facts_t* f,uint16_t* word) {
    *word=f->lying?T3_DUMMY_UP:0u;
    return !f->lying;
}

t3_practice_dummy_out_t t3_practice_dummy_input(const t3_practice_dummy_facts_t* f,unsigned setting) {
    /* _dummy_control: a scripted dummy is no human guard to start with. */
    t3_practice_dummy_out_t out={0,0};
    if(setting>=T3_DUMMY_CONTROLLER)return (t3_practice_dummy_out_t){0,1};
    if(f->doctor_b)return out;
    if(setting==T3_DUMMY_UKEMI_MAE || setting==T3_DUMMY_UKEMI_OKU) {
        if(f->in_reaction) {
            /* FUN_800B8AEC: LK+RK (forward) or LP+RP (backward) on alternate frames. */
            if(!f->move_pending && f->has_air_window && f->has_tech_roll && f->frame_odd)
                out.word=setting==T3_DUMMY_UKEMI_MAE?0x60u:0x90u;
        } else standing_ok(f,&out.word);
        return out;
    }
    if(!standing_ok(f,&out.word))return out;
    const int active=f->pose_frame<=f->active_last;
    switch(setting) {
    case T3_DUMMY_CROUCH:out.word=T3_DUMMY_DOWN;break;
    case T3_DUMMY_STAND_GUARD:if((f->attack&0x10u) && active)out.word=T3_DUMMY_BACK;break;
    case T3_DUMMY_CROUCH_GUARD:
        out.word=T3_DUMMY_DOWN;
        if((f->attack&0x08u) && active)out.word=T3_DUMMY_DOWN|T3_DUMMY_BACK;
        break;
    case T3_DUMMY_GUARD_ALL:
        if((f->attack&0x18u) && active)out.word=(f->attack&0x10u)?T3_DUMMY_BACK:(T3_DUMMY_DOWN|T3_DUMMY_BACK);
        break;
    case T3_DUMMY_AUTO_GUARD:out.human_guard=1;break;
    default:break;
    }
    return out;
}
