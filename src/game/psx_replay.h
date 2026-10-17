#ifndef T3_PSX_REPLAY_H
#define T3_PSX_REPLAY_H
#include "psx_pose.h"
enum {T3_REPLAY_FRAMES=300,T3_REPLAY_HALF_FRAMES=600};
enum {T3_REPLAY_PLAYING=1,T3_REPLAY_STARTING=2,T3_REPLAY_FIRST=3,
      T3_REPLAY_ENDING=4,T3_REPLAY_DONE=5,T3_REPLAY_RECORDING=7};
/* Portable normal-Paul pose history, not the original pointer-bearing wire
 * format. Slots and blend row tokens refer to the caller's immutable banks.
 * Hands/wings, sound and effect logs are separate presentation work. */
typedef struct {
    uint32_t slot,src_row,dst_row;
    t3_vec3i_t root,anchor;
    uint16_t tilt_x,heading,tilt_z;
    uint8_t src_frame,dst_frame,pose_frame,blend_frames,blend_bits,power;
} t3_psx_replay_frame_t;
typedef struct {
    uint32_t epoch;
    t3_psx_replay_frame_t actor[2];
} t3_psx_replay_pair_t;
/* Zero initialize once; single match owner. Keep outside the per-tick match
 * copy (e.g. Low WRAM). Epochs invalidate history without clearing 30 KiB. */
typedef struct {
    uint32_t epoch;
    t3_psx_replay_pair_t ring[T3_REPLAY_FRAMES];
} t3_psx_replay_storage_t;
typedef struct {
    int32_t mode,record,play,mark,recorded,skip;
    uint32_t epoch;
    t3_psx_replay_frame_t saved[2];
} t3_psx_replay_t;
void t3_psx_replay_reset(t3_psx_replay_t* state,uint32_t epoch);
int t3_psx_replay_mark_ko(t3_psx_replay_t* state,unsigned opponent_pose);
int t3_psx_replay_start(t3_psx_replay_t* state);
/* -1 invalid, 0 live, 1 playback. ENDING -> DONE clears blends in caller. */
int t3_psx_replay_update(t3_psx_replay_t* state);
/* Read only the current epoch, never stale slots from an earlier round. */
const t3_psx_replay_pair_t* t3_psx_replay_at(const t3_psx_replay_storage_t* storage,
                                           const t3_psx_replay_t* state,unsigned index);
t3_vec3i_t t3_psx_replay_root(t3_vec3i_t first,t3_vec3i_t next,unsigned half);
#endif
