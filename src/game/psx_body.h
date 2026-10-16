#ifndef T3_PSX_BODY_H
#define T3_PSX_BODY_H
#include "psx_ground.h"

enum {T3_PSX_BODY_SPHERES=8,T3_PSX_BODY_SQRT_BYTES=384};
typedef struct {
    t3_vec3i_t center;
    uint16_t radius;
} t3_psx_body_sphere_t;
typedef struct {
    t3_vec3i_t anchor,root,previous_root,placed,push;
    t3_psx_body_sphere_t spheres[T3_PSX_BODY_SPHERES];
    uint16_t facing;
    uint8_t active,invulnerable,no_push,active_first,contact,contact_with;
    int32_t ramp;
    uint32_t distance;
    uint16_t target_dir;
} t3_psx_body_actor_t;
typedef struct {
    t3_psx_ground_tables_t ground;
    const uint8_t* sqrt;size_t sqrt_size;
} t3_psx_body_tables_t;
/* Original SquareRoot0 lookup (not mathematical integer sqrt). Negative
 * radicands are outside this grounded path; failure does not modify output. */
int t3_psx_body_sqrt(const t3_psx_body_tables_t* tables,uint32_t n,uint32_t* out);
/* Normal two-fighter modes only; throw links, Tekken Ball and Force excluded.
 * Input sphere positions use PS1 coordinates. Updates are atomic. */
/* Profile applies to eight original joint-slot origins and Paul radius data. */
int t3_psx_body_shapes(t3_psx_body_actor_t* actor,const t3_psx_joint_t parts[T3_PSX_DRAW_PARTS],
                        uint32_t valid_parts,const uint8_t shape_tables[48],
                        const t3_move_record_t* move,int16_t frame,uint8_t in_air,uint8_t was_hit);
int t3_psx_body_separate(t3_psx_body_actor_t actors[2],uint32_t pair_distance,
                         const t3_psx_body_tables_t* tables);
int t3_psx_arena_bounds(t3_psx_body_actor_t* actor,const t3_psx_body_actor_t* opponent,
                         const t3_psx_body_tables_t* tables);
#endif
