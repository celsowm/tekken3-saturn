#ifndef T3_PSX_HEAD_H
#define T3_PSX_HEAD_H
#include "psx_body.h"
typedef struct {
    int16_t current[4],target[4];
    uint8_t active,snap,hold;
} t3_psx_head_t;
/* Normal Paul fight camera. Previous world joints are consumed before the
 * caller composes this actor's new world joints. Disabled leaves state intact. */
int t3_psx_head_look(t3_psx_head_t* state,const t3_psx_body_tables_t* tables,
                     const t3_psx_joint_t own[T3_PSX_DRAW_PARTS],
                     const t3_psx_joint_t opponent[T3_PSX_DRAW_PARTS],
                     const t3_move_record_t* move,int frame,uint8_t disabled,
                     t3_psx_matrix_t* head_local);
#endif
