#ifndef T3_SATURN_FIGHTER_SHADOW_H
#define T3_SATURN_FIGHTER_SHADOW_H
/* Fighter mesh shadows (docs/FIGHTER_FIDELITY.md): the original's TK3psSDW mesh
 * flattened onto the floor by the stage light's shear. */
#include <stdint.h>
#include "psx_camera.h"
#include "psx_pose.h"
#include "saturn/scene.h"

/* The most faces a pack's shadow mesh may have (the scene reserves two). */
#define T3_SHADOW_MAX_FACES 80u
/* Per call: calls, meshes queued, faces queued, faces behind the eye,
 * the first slot's centred screen x and y, unused, the last refusal. */
extern volatile int32_t g_t3_shadow_probe[8];
/* The stage's floor shear, before the fight. */
void t3_fighter_shadow_stage(unsigned stage);
/* Queues an actor's shadow mesh in `colour` behind everything else in the
 * scene, projected through the original view and its 18 world joints.
 * 0 when its pack has no shadow mesh or a joint is out of range. */
int t3_fighter_shadow_submit(sat_scene_t* scene,unsigned actor,const t3_psx_camera_matrix_t* view,
                             const t3_psx_joint_t joints[T3_PSX_DRAW_PARTS],uint16_t colour);
#endif
