/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Practice's REPLAY SETTINGS (practice.ovl FUN_800B8D50), adapted from Tekken3Remake
 * remake/core/game/practice_mode.gd (_replay_control) and modes.md#practice; see NOTICE.md. */
#ifndef T3_PRACTICE_REPLAY_H
#define T3_PRACTICE_REPLAY_H
#include <stdint.h>

/* S+0x81: OFF, after a combo of 4, 5 or 6 hits, or manual (Select). */
enum { T3_REPLAY_OFF=0,T3_REPLAY_COMBO4=1,T3_REPLAY_COMBO5=2,T3_REPLAY_COMBO6=3,T3_REPLAY_MANUAL=4,T3_REPLAY_SETTINGS=5 };
enum { T3_PRACTICE_REPLAY_MIN=0x5A,T3_PRACTICE_REPLAY_MAX=300 };
typedef struct { uint16_t replay_frames,combo_frames; } t3_practice_replay_t;   /* S+0x10, S+0x12 */
typedef struct {
    uint8_t setting;
    uint8_t paused,replaying;         /* the pause menu is up; a replay is playing */
    uint8_t select_pressed;
    uint8_t combo,last,ended;         /* the defender's data block: its combo, the last frame's combo, an ended combo's hits */
    uint8_t acting;                   /* S+0xAA for the player (FUN_800B4818) */
    uint16_t move_length;             /* the player's move length */
} t3_practice_replay_in_t;
/* One frame. Returns the number of frames to play back (`frames`, the caller starts a replay of frames-2), or 0. */
unsigned t3_practice_replay_step(t3_practice_replay_t* r,const t3_practice_replay_in_t* in,uint8_t* dropped);
#endif
