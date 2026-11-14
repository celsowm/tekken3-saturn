/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from Tekken3Remake remake/core/game/practice_mode.gd; see practice_replay.h and NOTICE.md. */
#include "practice_replay.h"

unsigned t3_practice_replay_step(t3_practice_replay_t* r,const t3_practice_replay_in_t* in,uint8_t* dropped) {
    /* D_DROPPED: the hits of a combo that just ended (the combo is 0 and the last frame's was not). */
    *dropped=(in->combo==0 && in->last!=0)?in->last:0u;
    if(in->replaying)return 0;
    if(!in->paused) {
        if(in->setting==T3_REPLAY_MANUAL)r->combo_frames=0;
        else if(*dropped==0 && in->combo==0 && in->acting)r->combo_frames=in->move_length;
        else ++r->combo_frames;
        if(r->replay_frames<T3_PRACTICE_REPLAY_MAX)++r->replay_frames;
    } else r->combo_frames=0;
    if(r->replay_frames<T3_PRACTICE_REPLAY_MIN)return 0;
    int start=0;
    switch(in->setting) {
    case T3_REPLAY_COMBO4:start=in->ended>=4u;break;
    case T3_REPLAY_COMBO5:start=in->ended>=5u;break;
    case T3_REPLAY_COMBO6:start=in->ended>=6u;break;
    case T3_REPLAY_MANUAL:
        if(in->select_pressed) {
            if(r->replay_frames>0x118u)r->replay_frames=0x118u;
            start=r->replay_frames>=T3_PRACTICE_REPLAY_MIN;
        }
        break;
    default:break;
    }
    if(!start)return 0;
    unsigned frames=r->replay_frames;
    if(in->setting!=T3_REPLAY_MANUAL && r->combo_frames!=0)frames=(unsigned)r->combo_frames+0x1Eu;
    if(frames>r->replay_frames)frames=r->replay_frames;
    if(frames<T3_PRACTICE_REPLAY_MIN)frames=T3_PRACTICE_REPLAY_MIN;
    if(frames>T3_PRACTICE_REPLAY_MAX)frames=T3_PRACTICE_REPLAY_MAX;
    r->replay_frames=0;r->combo_frames=0;
    return frames;
}
