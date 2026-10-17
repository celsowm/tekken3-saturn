#ifndef T3_PSX_MATCH_H
#define T3_PSX_MATCH_H
#include "psx_ground.h"
#include "psx_round.h"
#include "psx_root.h"
#include "psx_body.h"
#include "psx_ik.h"
#include "psx_exact_contact.h"
#include "psx_hit_test_gate.h"
#include "psx_hit_apply.h"
#include "psx_move_events.h"
#include "psx_blend.h"
#include "psx_head.h"

/* InputSource control, not a physical pad button. The owned core consumes it
 * after physics at the original input phase. All pad bits are ignored on clear. */
enum { T3_PSX_INPUT_CLEAR = 1u << 15 };

typedef struct {
    t3_vec3i_t anchor,root;
    uint16_t facing,heading;
} t3_psx_spawn_t;
typedef struct {
    const t3_divmot_t *bank,*common;
    t3_psx_ground_tables_t ground_tables;
    t3_motion_tables_t spline;
    const t3_command_sequence_t* sequences;size_t sequence_count;
    t3_psx_spawn_t spawn[2];
    unsigned scale_percent;
    int32_t max_health_q16;
    /* Optional full original world pose + normal-mode body/arena integration. */
    const t3_psx_part_t* parts[2]; /* Each actor's model, even in mirror matches. */
    t3_psx_ik_tables_t ik;
    t3_psx_rsqrt_t rsqrt; /* Optional original transition blend, requires body poses. */
    const uint8_t* body_tables;size_t body_tables_size;
    uint8_t enable_body,enable_combat;
    const uint8_t* reactions;size_t reactions_size;
    uint8_t chip_enabled,collapse_on_ko;
    uint8_t enable_rounds; /* Temporary slice policy; see psx_round.h. */
} t3_psx_match_config_t;
typedef struct {
    t3_move_vm_t vm;
    t3_psx_ground_state_t ground;
    t3_input_history_t input;
    t3_motion_cache_t root_cache,pose_cache;
    t3_psx_joint_t world[T3_PSX_DRAW_PARTS]; /* Original joint-slot order, not KMD mesh order. */
    int16_t pose[T3_PSX_POSE_CHANNELS],fk_root_xx;
    uint32_t fk_mask,ik_mask,parts_mask;
    uint8_t pose_frames;
    t3_psx_body_actor_t body;
    t3_psx_blend_t blend;
    t3_psx_head_t head;
    uint8_t no_look_at;
    uint8_t blend_guarded_previous,blend_in_reaction;
    int32_t ramp_step;
    uint32_t body_contacts;
    t3_vec3i_t previous_anchor;
    t3_world_attack_t strike;
    t3_psx_shadow_result_t geometry;
    t3_psx_hit_test_gate_t hit_gate;
    uint32_t contact_frames,gate_frames,registered_hits,registered_blocks,unsupported_moves;
    uint8_t hit_done,contact,contact_this_move,whiffed,ko_collapse;
    t3_psx_hit_apply_result_t hit;
    t3_psx_move_events_t events;
    t3_psx_reaction_pick_t reaction;
    int32_t health_q16;
    uint8_t got_hit;int16_t hit_freeze_in;
    uint16_t previous_pose_frame;uint8_t started;
} t3_psx_fighter_t;
typedef struct {
    const t3_psx_match_config_t* config;
    t3_psx_fighter_t fighter[2];
    uint32_t ticks;
    t3_psx_round_t round;
} t3_psx_match_t;

/* Experimental owned original state. Config/banks/tables must outlive it.
 * Currently integrates move starts, ground movement, original root placement
 * poses, body/arena, exact contact/gate queries and end-of-tick input queues.
 * Optional bounded combat activates original damage, grounded reactions and
 * USA VS KO collapse, with an optional bounded round/reset policy.
 * Original round-flow/full replay parity are not yet complete. */
int t3_psx_match_init(t3_psx_match_t* match,const t3_psx_match_config_t* config);
/* Pair-atomic normal two-fighter HitApply + queued reactions. Consumes the
 * two current gates and MoveEvents facts. Normal reactions are queued; USA VS
 * KO collapse starts immediately. No physics/input step is advanced. */
int t3_psx_match_apply_hits(t3_psx_match_t* match);
int t3_psx_match_step(t3_psx_match_t* match,t3_input_t first,t3_input_t second);
#endif
