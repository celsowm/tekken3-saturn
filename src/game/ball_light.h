#ifndef T3_BALL_LIGHT_H
#define T3_BALL_LIGHT_H
/* Tekken Ball's per-vertex lighting (volley.ovl FUN_800B3198's ColorMatCol, projection_sim.color_mat_col): the stage's
 * main light and its fill light from below, as the GTE's LIGHT, SQR and CC steps. */
#include <stdint.h>

typedef struct {
    int32_t light[3];          /* the stage's main light row (4096 = unit length), before the ball doubles it */
    uint8_t colour[3];         /* the main light's colour R, G, B (the fill light has half of it) */
    uint8_t back[3];           /* the back colour: the type's (or kind 4's pulse) */
    uint8_t power;             /* the type's specular power: SQR runs max(power, 1) times */
    uint8_t fill;              /* 0: no fill light (stage 17) */
} t3_ball_light_t;

/* The light's strength at a normal (s16 per axis, 4096 = 1), as the colour step leaves it before the face's colour: 4096 = 1. */
void t3_ball_light_vertex(const t3_ball_light_t* l,const int32_t normal[3],int32_t out[3]);
/* A face colour byte lit by `light` (t3_ball_light_vertex): 0..255. */
unsigned t3_ball_light_channel(unsigned face,int32_t light);
#endif
