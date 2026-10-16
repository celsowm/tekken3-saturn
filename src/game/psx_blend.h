#ifndef T3_PSX_BLEND_H
#define T3_PSX_BLEND_H
#include "psx_blend_math.h"

enum { T3_BLEND_NONE=0,T3_BLEND_DECAY=1,T3_BLEND_BRANCH=2,T3_BLEND_STANCE=3 };
typedef struct {
    t3_psx_matrix_t previous[17],delta[17];
    int16_t previous_root[3],root_delta[3],weight;
    int32_t frames,counter,last_frame;
    uint32_t last_row,src_row,dst_row;
    uint8_t mode,active,src_frame,dst_frame;
} t3_psx_blend_t;
typedef struct {
    uint32_t row,pending_row,stance_row;
    int16_t pose_frame,frame_step,entry_frame,previous_event_frame,decoded_root_dy;
    int16_t throw_state;
    uint8_t length,active_first,changed,branch_kind,transition,pending;
    uint8_t suppressed,no_forward;
} t3_psx_blend_facts_t;
/* Callback decodes arbitrary row identity/frame with actor-scaled root channels
 * and original FK/IK matrices (18 slots). No blend/head-look is applied here. */
typedef int (*t3_psx_blend_sample_t)(void*,uint32_t,unsigned,int16_t[3],t3_psx_matrix_t[18]);
/* Normal live match only; global replay playback/hold are not enabled by this
 * bounded path. Failure may modify state: invoke inside the match transaction. */
int t3_psx_blend_prepare(t3_psx_blend_t* state,const t3_psx_blend_facts_t* facts,
                         t3_psx_blend_sample_t sample,void* user);
/* RootUpdate sets the matrix weight and displayed Y using truncation toward0.
 * Pose composition then blends slots1..17 and saves them BEFORE head tracking. */
int16_t t3_psx_blend_root_y(t3_psx_blend_t* state,int16_t scaled_y);
int t3_psx_blend_locals(t3_psx_blend_t* state,const t3_psx_rsqrt_t* table,
                        t3_psx_matrix_t slots[18]);
#endif
