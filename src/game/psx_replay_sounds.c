/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Replay sound scheduling adapted from pinned Tekken3Remake replay_state.gd
 * and sound.md (original 0x80033780, 0x80033838, 0x800338E0).
 * Changes: caller-owned numeric words, explicit cursor/bank selection and
 * logical playback packets; no pointer-bearing PS1 storage or audio backend.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "psx_replay_sounds.h"
#include <limits.h>

static int valid(const uint32_t* ring,const int32_t* delay,unsigned record) {
    return ring && delay && record<T3_REPLAY_SOUND_FRAMES &&
        *delay>=0 && *delay<=INT32_MAX-T3_REPLAY_SOUND_FRAMES;
}
int t3_psx_replay_sound_record(uint32_t ring[T3_REPLAY_SOUND_FRAMES],int32_t* delay,
    int32_t mode,unsigned record,unsigned kind,unsigned player,unsigned code) {
    if(mode!=7)return 1;
    if(!valid(ring,delay,record))return 0;
    const unsigned slot=(record+(unsigned)*delay)%T3_REPLAY_SOUND_FRAMES;
    ring[slot]=(uint32_t)(code&0xFFFFu)|((uint32_t)(kind&0xFFu)<<16)|((uint32_t)(player&0xFFu)<<24);
    ++*delay;
    return 1;
}
int t3_psx_replay_sound_clear(uint32_t ring[T3_REPLAY_SOUND_FRAMES],int32_t* delay,unsigned record) {
    if(!valid(ring,delay,record))return 0;
    if(*delay>0)--*delay;
    ring[(record+(unsigned)*delay)%T3_REPLAY_SOUND_FRAMES]&=0xFF00FFFFu;
    return 1;
}
int t3_psx_replay_sound_at(const uint32_t ring[T3_REPLAY_SOUND_FRAMES],unsigned play,
    t3_psx_replay_sound_t* sound) {
    if(!ring || !sound || play>=2*T3_REPLAY_SOUND_FRAMES)return -1;
    if(play&1u)return 0;
    const uint32_t packed=ring[play>>1];
    const unsigned kind=(packed>>16)&0xFFu;
    if(kind<1 || kind>3)return 0;
    *sound=(t3_psx_replay_sound_t){(uint8_t)kind,kind==1?0:(uint8_t)(packed>>24),(uint16_t)packed};
    return 1;
}
