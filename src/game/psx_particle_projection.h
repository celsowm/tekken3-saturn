#ifndef T3_PSX_PARTICLE_PROJECTION_H
#define T3_PSX_PARTICLE_PROJECTION_H
#include "psx_camera.h"

/* EffectsUpdate's camera-relative billboard transform, distinct from the
 * fighter view. Build once per image; origin is retained for s16 subtraction. */
typedef struct {
    int16_t m[3][3];
    int32_t origin[3],h;
} t3_psx_particle_view_t;
typedef struct { int16_t corners[4][4]; } t3_psx_particle_model_t;
typedef struct {
    /* PS1 POLY_FT4 order: TL, TR, BL, BR. Not VDP1's perimeter order. */
    int16_t xy[4][2];
    uint16_t depth[4];
    int16_t bucket;
    uint8_t visible;
} t3_psx_particle_quad_t;

int t3_psx_particle_build_view(const t3_psx_camera_view_t* camera,
                              const t3_psx_trig_t* trig,t3_psx_particle_view_t* out);
/* Computes all vertices even when culled. flags bit 0x20 biases the OT bucket
 * by -5. Caller handles sprite-enable flag 0x40 and global draw suppression.
 * Original fixed screen offsets are (192,240); output stays in PS1 units.
 * Returns zero only for invalid arguments, without changing the output. */
int t3_psx_particle_project(const t3_psx_particle_view_t* view,const int32_t position[3],
                            const t3_psx_particle_model_t* model,unsigned flags,
                            t3_psx_particle_quad_t* out);
#endif
