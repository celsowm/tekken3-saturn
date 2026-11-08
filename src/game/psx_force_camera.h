/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of Tekken Force's side-scrolling camera (force.ovl FUN_800B545C and FUN_800B5504, Japan
 * Rev.1) as read from the original code and described in the pinned Tekken3Remake (remake/core/force/
 * tekken_force.gd, docs/research/code/camera.md and modes.md). Verified against the original code in the
 * CPU harness (scripts/export_force_camera_cases.py). */
#ifndef T3_PSX_FORCE_CAMERA_H
#define T3_PSX_FORCE_CAMERA_H
#include <stdint.h>

/* The camera record at 0x800B7098. The source handed to the fight camera (CameraSetSource,
 * CameraUseSource(0)) is x, y, z, pitch, yaw, h. */
typedef struct {
    uint32_t player;      /* the followed fighter record (an address in the original) */
    int32_t x,y,z,pitch,yaw,h;
    int32_t mode;         /* scroll mode: 0 free, 1 locked, 2 locked within min..max */
    int32_t target_x,pitch_target;
    int32_t left,right;   /* 0x800B70C0, 0x800B70C4: screen x limits; right is 1 or 60 */
    int32_t min,max;      /* mode 2: the camera x limits */
} t3_force_camera_t;

/* FUN_800B545C: the camera starts on `player` at x 0, looking down at pitch 0x2AAA. Returns the roll
 * (0x800A8A90: 0x82 on level 4, else 0); `start_x` (0x800B70E4) is the camera's x, here 0. */
int32_t t3_force_camera_setup(t3_force_camera_t* c,uint32_t player,int level4,int32_t* start_x);

/* FUN_800B5504: the camera follows the player's projected screen x (mode 0 past the centre, mode 2 within its
 * bounds) and pitches back when the player nears the bottom of the screen. `trig` is the 0x2800-byte sine
 * table of the executable (0x8001E8C4: s16 at 2 * (angle & 4095), cosine 0x800 bytes further). */
void t3_force_camera_frame(t3_force_camera_t* c,const uint8_t* trig,int32_t roll,int32_t player_x,int32_t player_z);

/* The same follow projection for any world point (the fighters' stored screen x). */
int32_t t3_force_camera_screen_x(const t3_force_camera_t* c,const uint8_t* trig,int32_t roll,
                                 int32_t px,int32_t py,int32_t pz);
#endif
