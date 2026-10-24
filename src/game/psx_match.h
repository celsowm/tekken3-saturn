#ifndef T3_PSX_MATCH_H
#define T3_PSX_MATCH_H
#include "psx_ground.h"
#include "psx_round.h"
#include "psx_round_flow.h"
#include "psx_replay.h"
#include "psx_root.h"
#include "psx_body.h"
#include "psx_ik.h"
#include "psx_exact_contact.h"
#include "psx_hit_test_gate.h"
#include "psx_hit_apply.h"
#include "psx_move_events.h"
#include "psx_blend.h"
#include "psx_head.h"
#include "psx_local_pose.h"
#include "psx_world_pose.h"
#include "psx_fight_camera.h"

/* Throw situation code (0x800313B0), branch conditions 2..0x17. */
int16_t t3_psx_throw_situation(const t3_psx_ground_state_t* self,int contact,
                               const t3_psx_ground_state_t* opp);

/* InputSource control, not a physical pad button. The owned core consumes it
 * after physics at the original input phase. All pad bits are ignored on clear. */
enum { T3_PSX_INPUT_CLEAR = 1u << 15 };

typedef struct {
    t3_vec3i_t anchor,root;
    uint16_t facing,heading;
} t3_psx_spawn_t;
typedef struct {
    /* `bank` is both fighters' character bank unless actor_bank names one. */
    const t3_divmot_t *bank,*common;
    const t3_divmot_t* actor_bank[2];
    uint8_t character[2]; /* character ids, for restrictions 0x46..0x5C */
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
    /* 1: every attack may start (matched against original-code fights,
     * docs/FIGHT_ORACLE.md); 0: only the first qualified Paul attacks
     * (slots 295, 355, 357). Reversals are not yet ported. */
    uint8_t all_attacks;
    const uint8_t* reactions;size_t reactions_size;
    /* g_closeReactions: s16 reaction, s16 distance per move +0x34 index. */
    const uint8_t* close_reactions;size_t close_reactions_size;
    /* g_throwVictimSlots (0x800174C4): s16 victim slots of move +0x32 0x1nnn. */
    const uint8_t* throw_victim_slots;size_t throw_victim_slots_size;
    uint8_t chip_enabled,collapse_on_ko;
    uint8_t enable_rounds; /* 0 none, 1 bounded slice, 2 original frame API. */
    t3_psx_replay_storage_t* replay_storage;
    t3_motion_sample_cache_t* sample_cache; /* Optional, derived, immutable-bank lifetime. */
    t3_psx_local_pose_cache_t* local_pose_cache; /* Optional raw FK/IK only, same lifetime contract. */
    t3_psx_world_pose_cache_t* world_pose_cache; /* Optional exact post-blend composition, derived storage. */
    /* Optional original fight camera (enable_rounds=2 and the body only). */
    const t3_psx_fight_camera_config_t* camera;
    /* Optional CPU opponents (psx_ai_game.h), caller-owned mutable storage
     * like replay_storage; requires enable_rounds=2 and combat. */
    struct t3_psx_ai_game* ai;
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
    /* The limb slots (4,5,8,9,12,13,15,16) of the last displayed local pose:
     * the original slot buffer (+0xF74) keeps them when IK cannot solve. */
    t3_psx_matrix_t limbs[8];uint8_t limbs_valid;
    t3_psx_head_t head;
    uint8_t no_look_at;
    uint8_t blend_guarded_previous,blend_in_reaction;
    int32_t ramp_step;
    uint32_t body_contacts;
    t3_vec3i_t previous_anchor;
    t3_world_attack_t strike;
    uint8_t strike_joint; /* the sampled descriptor's first joint (hand below 12, foot from 12) */
    t3_psx_shadow_result_t geometry;
    t3_psx_hit_test_gate_t hit_gate;
    uint32_t contact_frames,gate_frames,registered_hits,registered_blocks,unsupported_moves;
    /* Diagnostics: the last move dropped as unsupported. */
    uint16_t unsupported_slot,unsupported_transition;
    uint8_t side_flag; /* +0xC6, FighterUpdateSide: the stick mirrors on side 1 */
    uint8_t hit_done,contact,contact_this_move,whiffed,ko_collapse;
    t3_psx_hit_apply_result_t hit;
    t3_psx_move_events_t events;
    t3_psx_reaction_pick_t reaction;
    int32_t health_q16;
    uint8_t got_hit;int16_t hit_freeze_in;
    uint8_t last_guarded,last_counter; /* +0xCF/+0xD3: valid on the hit frame only (cleared per frame) */
    uint16_t previous_pose_frame;uint8_t started;
} t3_psx_fighter_t;
typedef struct t3_psx_match {
    const t3_psx_match_config_t* config;
    t3_psx_fighter_t fighter[2];
    uint32_t ticks;
    t3_psx_round_t round;
    t3_psx_round_flow_t flow;
    t3_psx_round_actor_t round_actor[2];
    uint32_t fight_clock;
    t3_psx_replay_t replay;
    int32_t intro_camera_left,replay_skip;
    uint8_t round_gap; /* FightMain frames since the round ended (0..2) */
    /* Without a fight camera: the screen x supplied by the frames. */
    uint8_t screen_known;int16_t screen_x[2];
    /* Diagnostic: this tick's fighter draw consumed frame->fighter_view
     * (the host exports that view into the sparse reference fixture). */
    uint8_t frame_view_used;
    uint32_t pair_distance; /* PairwiseDistances' pair table (0x8009EA08) */
    uint32_t frame_counter; /* g_frameCounter of the frame being stepped */
    uint32_t fighter_distance; /* g_fighterDistance: fighter 0's after PairwiseDistances */
    /* Fighters in a throw after MoveStartAll (0x800958C4), the count the link
     * last saw, and the link that moves both by arena corrections. */
    uint8_t throw_count,throw_count_seen,throw_link;
    t3_psx_fight_camera_t camera;
} t3_psx_match_t;
typedef struct {
    uint32_t frame_counter;
    uint16_t physical[2],pressed[2];
    /* Without a fight camera there is no screen position: side_known supplies
     * each fighter's side for this frame (otherwise the side is kept). */
    uint8_t side_known,side[2];
    /* Likewise each fighter's screen x (+0x1278) after the previous frame,
     * for PairwiseDistances' off-screen rule. */
    uint8_t screen_known;int16_t screen_x[2];
    /* Without a fight camera: the fighter view the previous frame left
     * (0x800AE2E8 with H), which a draw before CameraFrame uses; NULL when
     * unknown. */
    const t3_psx_camera_matrix_t* fighter_view;
} t3_psx_match_frame_t;

static inline const t3_divmot_t* t3_psx_match_bank(const t3_psx_match_config_t* c,unsigned i) {
    return c->actor_bank[i]?c->actor_bank[i]:c->bank;
}

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
/* BranchCondition's facts for fighter i from the current state (combat). */
void t3_psx_match_branch_facts(const t3_psx_match_t* match,unsigned i,struct t3_branch_context* facts);
int t3_psx_match_step(t3_psx_match_t* match,t3_input_t first,t3_input_t second);
/* Original RoundFlow precedes movement. Inputs are external, mapped relative
 * pad samples; input mode 0/2 is generated by the native round controller.
 * Requires enable_rounds=2. Includes selective later-round placement/intro.
 * KO playback requires zero-initialized caller-owned replay storage. An
 * unavailable history/unsupported pose fails atomically. The terminal result
 * screen keeps an input tick clock without advancing fighters. No original
 * fighter or round state is supplied per tick. */
int t3_psx_match_step_original(t3_psx_match_t* match,t3_input_t first,t3_input_t second,
                               const t3_psx_match_frame_t* frame);
/* The same step without the atomic copy: a failure may leave `match` partly
 * advanced. For callers that treat any failure as fatal. */
int t3_psx_match_step_original_in_place(t3_psx_match_t* match,t3_input_t first,t3_input_t second,
                                        const t3_psx_match_frame_t* frame);

/* The fight camera's parts of the original step (psx_match_camera.c); each
 * does nothing without a camera config and returns 0 on invalid state.
 * Round start (FUN_8002AB68), after the fighters are placed. */
int t3_psx_match_camera_round(t3_psx_match_t* match,int first);
/* A live frame after RoundFlow: CameraDirector, CameraShakeStep, CameraFrame. */
int t3_psx_match_camera_frame(t3_psx_match_t* match);
/* A replay frame: `before` holds the fighters captured before the replay
 * renders the frame; `first` on the replay's first frame. */
void t3_psx_match_camera_capture(const t3_psx_match_t* match,t3_psx_camera_fighter_t before[2]);
int t3_psx_match_camera_replay(t3_psx_match_t* match,t3_psx_camera_fighter_t before[2],int first);
/* After the fighters moved: their screen x and, when live, the shakes they start. */
int t3_psx_match_camera_after(t3_psx_match_t* match,int live);
/* The view the fighters are drawn with (FUN_80036254). */
void t3_psx_match_fighter_view(const t3_psx_fight_camera_t* camera,t3_psx_camera_matrix_t* out);
/* FighterDrawParts (0x80037B78): drawing part 11 stores fighter i's screen
 * x; an inactive fighter or one without the part keeps it. */
void t3_psx_match_draw_screen(const t3_psx_match_t* match,unsigned i,
                              const t3_psx_camera_matrix_t* view,int16_t* screen_x);
#endif
