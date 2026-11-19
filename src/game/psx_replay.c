/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Normal VS replay cursor/KO mark/interpolation adapted from pinned
 * Tekken3Remake replay_state.gd. Changes: portable value-owned metadata,
 * caller-owned history with epoch validation, no audio/effect/hand log;
 * Tekken Ball's window, speed and early end.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "psx_replay.h"
static int mod600(int n){int m=n%600;return m<0?m+600:m;}
static int valid(const t3_psx_replay_t* s) {
    return s && s->record>=0 && s->record<300 && s->play>=0 && s->play<600 &&
        s->mark>=-2 && s->mark<600 && s->recorded>=0 && s->recorded<=0x70000000 &&
        ((s->mode>=1 && s->mode<=5) || s->mode==7);
}
void t3_psx_replay_reset(t3_psx_replay_t* s,uint32_t epoch) {
    *s=(t3_psx_replay_t){.mode=T3_REPLAY_RECORDING,.mark=-2,.epoch=epoch};
}
int t3_psx_replay_mark_ko(t3_psx_replay_t* s,unsigned frame) {
    if(!valid(s) || frame>32767)return 0;
    s->mark=mod600((s->record-(int)frame-30)*2);return 1;
}
int t3_psx_replay_start_back(t3_psx_replay_t* s,int back) {
    if(!valid(s))return 0;
    if(s->recorded<back)back=s->recorded-2;
    if(back==0)return t3_psx_replay_start(s);
    if(back>0x12A)back=0x12A;
    if(back>0)s->mark=mod600((s->record-back+300)%300*2);
    int age=s->record-(s->mark>>1);
    if(age<0)age+=0x4B00;
    if(age%300<150 && s->recorded>152)s->mark=mod600((s->record-150)*2);
    s->mode=T3_REPLAY_STARTING;s->play=s->mark;
    return 1;
}
int t3_psx_replay_start(t3_psx_replay_t* s) {
    if(!valid(s))return 0;
    if(s->mark<0){s->mode=T3_REPLAY_DONE;s->mark=mod600(s->record*2-320);return 1;}
    int age=(s->record-(s->mark>>1)+19200)%300;
    if(age<150 && s->recorded>152)s->mark=mod600((s->record-150)*2);
    s->mode=T3_REPLAY_STARTING;s->play=s->mark;return 1;
}
int t3_psx_replay_start_ball(t3_psx_replay_t* s) {
    if(!t3_psx_replay_start(s))return 0;
    if(s->mode==T3_REPLAY_STARTING)s->mark=s->play=mod600((s->record-30)*2);
    return 1;
}
static int update(t3_psx_replay_t* s,int step,int end) {
    if(!valid(s))return -1;
    switch(s->mode) {
    case T3_REPLAY_PLAYING:
        s->play=(s->play+step)%600;
        if(s->play==mod600(s->record*2-end) || s->skip)s->mode=T3_REPLAY_ENDING;
        return 1;
    case T3_REPLAY_STARTING:s->mode=T3_REPLAY_FIRST;return 1;
    case T3_REPLAY_FIRST:s->mode=T3_REPLAY_PLAYING;s->skip=0;return 1;
    case T3_REPLAY_ENDING:s->mode=T3_REPLAY_DONE;return 0;
    case T3_REPLAY_RECORDING:
        if(s->recorded<0x70000000)++s->recorded;
        s->record=(s->record+1)%300;return 0;
    default:return 0;
    }
}
int t3_psx_replay_update(t3_psx_replay_t* s){return update(s,1,2);}
int t3_psx_replay_update_ball(t3_psx_replay_t* s,int shown,int shows) {
    return update(s,shown<shows?2:1,shown==3 && shows>3?0x20:2);
}
const t3_psx_replay_pair_t* t3_psx_replay_at(const t3_psx_replay_storage_t* storage,
                                           const t3_psx_replay_t* s,unsigned index) {
    if(!storage || !s || index>=300 || storage->epoch!=s->epoch ||
       storage->ring[index].epoch!=s->epoch)return 0;
    return &storage->ring[index];
}
t3_vec3i_t t3_psx_replay_root(t3_vec3i_t a,t3_vec3i_t b,unsigned half) {
    if(!half)return a;
    return (t3_vec3i_t){(int32_t)(((int64_t)a.x+b.x)/2),
        (int32_t)(((int64_t)a.y+b.y)/2),(int32_t)(((int64_t)a.z+b.z)/2)};
}
