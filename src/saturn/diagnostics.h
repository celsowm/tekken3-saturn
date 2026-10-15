#ifndef T3_SATURN_DIAGNOSTICS_H
#define T3_SATURN_DIAGNOSTICS_H

#include <stdint.h>

/* Integer-only externally discoverable probe for debug emulators. Fields
 * snapshot at the end of each work loop, before the next VDP1 submission.
 * frame_serial is committed LAST, so a debugger can check it before/after
 * reading the record. Loop rate is NOT necessarily visible screen FPS. */
#define T3_DIAGNOSTICS_MAGIC 0x54334431u /* T3D1 */
#define T3_DIAGNOSTICS_VERSION 11u

typedef struct {
    uint32_t magic, version;
    uint32_t frame_serial;
    uint32_t vblanks;
    uint32_t sim_ticks;
    uint32_t sim_steps_last_frame;
    uint32_t loop_rate_x10;
    uint32_t frame_work_ms;
    uint32_t faces_submitted;
    uint32_t faces_flushed;
    uint32_t faces_culled;
    uint32_t faces_rejected;
    uint32_t commands_used;
    uint32_t commands_capacity;
    uint32_t late_swaps;
    uint32_t draw_waits;
    uint32_t draw_timeouts;
    int32_t first_error;
    uint32_t psx_logic_active;
    uint16_t psx_move_slot[2], psx_move_frame[2], psx_attack_word[2];
    uint32_t psx_transitions[2];
    uint8_t psx_attack_segments[2];
    uint32_t psx_attack_decode_errors[2];
    uint8_t psx_pose_status[2],psx_pose_frames[2];
    int16_t psx_motion_root[2][3];
    uint32_t psx_fk_valid_slots[2];
    uint32_t psx_fk_valid_model_parts[2];
    uint32_t psx_ik_valid_slots[2];
    uint8_t psx_skin_preview_applied[2];
    uint8_t psx_hurt_zones_ready[2];
    int32_t psx_hurt_head_local_y[2];
    int16_t psx_fk_root_xx[2];
    uint8_t psx_shadow_contact_status[2];
    int8_t psx_shadow_contact_zone[2];
    uint32_t psx_shadow_contact_edges[2];
    uint32_t psx_shadow_overlap_frames[2];
    uint8_t psx_event_status[2],psx_extra_kind[2],psx_extra_damage[2];
    uint16_t psx_events_fired[2];
} t3_diagnostics_t;

extern volatile t3_diagnostics_t g_t3_diagnostics;

#endif /* T3_SATURN_DIAGNOSTICS_H */
