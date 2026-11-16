#ifndef T3_SATURN_FIGHTER_LIGHT_H
#define T3_SATURN_FIGHTER_LIGHT_H
/* Fighter lighting (docs/FIGHTER_FIDELITY.md): the original's per-normal light
 * as one VDP1 Gouraud table per face, relit from the PS1 joint rotations. */
#include <stdint.h>
#include "psx_pose.h"
#include "saturn/core.h"

/* The stage's light rig, before the fighters load. */
void t3_fighter_light_stage(unsigned stage);
/* Reserves an actor's tables in VDP1 VRAM, neutral until its first relight.
 * 0 when its pack has no lighting data (8x8 INDEX8 textures, which VDP1
 * Gouraud cannot shade); otherwise the instance's face_gouraud_first: face
 * f's table is that CMDGRDA plus f. */
uint16_t t3_fighter_light_load(unsigned actor);
/* Relights an actor's tables from its PS1 part rotations. Either CPU, one
 * actor each; it reads only the pack, the stage table and `joints`. */
void t3_fighter_light_relight(unsigned actor,const t3_psx_joint_t joints[T3_PSX_DRAW_PARTS]);
/* Practice's freeze signal: the actor's back colour (R, G, B bytes), or the stage's when `on` is 0. */
void t3_fighter_light_back(unsigned actor,int on,const uint8_t rgb[3]);
#endif
