#ifndef T3_PSX_MOVE_EVENTS_H
#define T3_PSX_MOVE_EVENTS_H
#include <stdint.h>
#include "divmot.h"

/* Original DIVMOT section 4, [(u16 frame,u16 command)] terminated
 * by frame==0. Each move row +0x20 indexes 4-byte event pairs, with
 * 0xFFFFFFFF = no authored list. The original executable copies the
 * most recently fired extra command (high byte 11,12,13) into extraKind,
 * and low byte into extraDamage. No side-effect from other commands.
 */
typedef struct {
    uint8_t extra_kind; /* 0 none, 1/2/3 for high byte 11/12/13 */
    uint8_t extra_damage; /* original low byte, zero -> live damage fallback */
    uint16_t extra_frame; /* last extra event's authored frame, 0 if none */
    uint16_t fired;       /* number of all frame events crossed */
    uint8_t shake;        /* 0 none, else 1 + the last camera shake script started (high byte 1/2/3) */
} t3_psx_move_events_t;

/* Evaluate event_frame in (previous_pose_frame, current_pose_frame].
 * Success 1 (including no events); 0 if authored source section 4 was
 * removed from a compact bank; -1 if truncated/malformed source data.
 * No output mutation on 0 or -1. No allocation, effects, frame sounds,
 * hand-shape changes or fight state transitions are performed.
 */
int t3_psx_move_events_sample(const t3_divmot_t* bank,
                              const t3_move_record_t* move,
                              unsigned previous_pose_frame,
                              unsigned current_pose_frame,
                              t3_psx_move_events_t* result);
/* Frames from `pose_frame` to the move's last event whose high byte is
 * `hi` (the camera's 0x0B/0x0C look-ahead), -1 for none. 0 on malformed
 * input or when section 4 was not imported. */
int t3_psx_move_events_last(const t3_divmot_t* bank,const t3_move_record_t* move,
                            unsigned hi,int pose_frame,int16_t* left);
#endif
