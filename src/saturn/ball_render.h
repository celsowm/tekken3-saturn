#ifndef T3_BALL_RENDER_H
#define T3_BALL_RENDER_H
#include "saturn/scene.h"
#include "ball_game.h"

/* Tekken Ball's 3D part of one image (volley.ovl FUN_800B3198 and FUN_800B3DA4): the ball (16 triangles, 32 quads, lit),
 * its shadow, and the court's centre band and side lines (one quad each), submitted to the scene in the world of the fighters' drop
 * shadows (96 units per original unit, x and -z about the fighters' centre). `trig` is the executable's sine table (the
 * ball's spin), `eye` the camera's position (the original's view x, y, z: the faces turned away are not drawn), `shadow` the stage's shadow colour (RGB555). Needs up to BALL_RENDER_FACES faces of the scene. */
enum { BALL_RENDER_FACES=48+2+2 };   /* the ball, its shadow's two quads, the court's two side lines */
sat_result_t t3_ball_render(sat_scene_t* scene,const t3_ball_view_t* view,const t3_ball_mesh_t* mesh,
                            const uint8_t* trig,const int32_t eye[3],int32_t center_x,int32_t center_z,uint16_t shadow);
#endif
