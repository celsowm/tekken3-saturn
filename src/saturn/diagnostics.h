#ifndef T3_SATURN_DIAGNOSTICS_H
#define T3_SATURN_DIAGNOSTICS_H

#include <stdint.h>

/* Integer-only externally discoverable probe for debug emulators. Fields
 * snapshot at the end of each work loop, before the next VDP1 submission.
 * snapshot_epoch is odd while publishing and even on commit. A running
 * debugger checks it before/after reading; a paused reader rejects odd.
 * Loop rate is NOT necessarily visible screen FPS. */
#define T3_DIAGNOSTICS_MAGIC 0x54334431u /* T3D1 */
#define T3_DIAGNOSTICS_VERSION 20u

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
    /* Version 12: fixed-width, big-endian gameplay snapshot. */
    uint32_t pad_connected[2], pad_held[2], pad_pressed[2];
    int32_t fighter_x[2], fighter_z[2];
    uint32_t health[2], attack[2], attack_age[2];
    uint32_t guarding[2], crouching[2], stun[2];
    uint32_t round, wins[2], round_done, match_done;
    int32_t winner;
    uint32_t timer, hits, blocks;
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
    uint32_t snapshot_epoch; /* odd while publishing; even = committed */
    uint32_t model_archive_bytes, model_texture_count, model_texture_vram_bytes;
    uint32_t skin_work_ms, render_work_ms, submitted_frames;
    uint32_t psx_lifecycle_active;
    int16_t psx_pending_slot[2],psx_root_frame[2],psx_move_age[2];
    int16_t psx_freeze[2],psx_hold_frames[2];
    uint32_t psx_owned_active;
    int32_t psx_health_q16[2],psx_world_root[2][3],psx_anchor[2][3];
    uint32_t psx_body_active,psx_body_contacts[2];
    int32_t psx_body_push[2][3];
    uint32_t psx_contact_active,psx_geometry_status[2];
    int32_t psx_contact_zone[2];
    uint32_t psx_hit_gate[2],psx_contact_frames[2];
    uint32_t psx_gate_frames[2]; /* eligibility queries, not registered damage */
    uint32_t psx_combat_active,psx_registered_hits[2],psx_registered_blocks[2],psx_unsupported_moves[2];
    int32_t psx_damage_last_tick[2],psx_reaction_slot[2],psx_reaction_transition[2];
    uint32_t psx_hit_done[2],psx_guarded_last_tick[2],psx_ko_collapse[2];
    uint32_t psx_round_flow_active;
    int32_t psx_round_state,psx_round_counter,psx_round_hold;
    int32_t psx_replay_mode,psx_replay_record,psx_replay_play,psx_replay_mark;
    uint32_t psx_replay_recorded,psx_replay_epoch;
} t3_diagnostics_t;

extern volatile t3_diagnostics_t g_t3_diagnostics;

#endif /* T3_SATURN_DIAGNOSTICS_H */
