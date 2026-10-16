#ifndef T3_MOVE_CLOCK_H
#define T3_MOVE_CLOCK_H
#include <stdint.h>

/* Original MoveStartOrAdvance's no-pending-move path. These are independent
 * signed 16-bit guest counters, not display frames or stun durations.
 * pose_frame is one-based in the original engine. The legacy observer's
 * zero-based initial pose remains supported during its migration. */
typedef struct {
    int16_t pose_frame, root_frame, move_frame;
    int16_t frame_step, hit_freeze, hold_frames;
    uint8_t ballistic, move_changed;
} t3_move_clock_t;

void t3_move_clock_reset(t3_move_clock_t* clock, int16_t pose_frame);
/* Mutates the owning fighter/VM clock once per LOGIC step. Returns 1 when
 * rootMove must become poseMove, 0 otherwise; -1 rejects malformed state
 * without writes. The caller owns both move identities. */
int t3_move_clock_advance(t3_move_clock_t* clock, uint8_t authored_hold_frame);

typedef struct {
    int16_t entry_frame,branch_window,hit_freeze_in;
    uint8_t transition,branch_kind;
    uint8_t current_length,next_length,root_length;
    uint8_t next_active_first,next_active_last;
    uint8_t same_animation,got_hit,alive;
} t3_move_start_t;

/* A queued move starts against the OLD pose counter, before advancement.
 * Zero frame_step starts immediately; backwards playback reverses the test. */
int t3_move_clock_pending_ready(const t3_move_clock_t* clock,
                                 int16_t entry_frame,uint8_t current_length);
/* Ground move-start CLOCK effects only. Returns rootMove binding as above.
 * Throw/launch starts (30..34,36) and unknown transitions are unsupported.
 * Tracking, reanchoring, pushback, state flags and input-buffer changes are
 * separate fighter responsibilities; this function never invents them. */
int t3_move_clock_begin(t3_move_clock_t* clock,const t3_move_start_t* start);
#endif
