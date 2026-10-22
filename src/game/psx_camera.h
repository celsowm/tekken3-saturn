#ifndef T3_PSX_CAMERA_H
#define T3_PSX_CAMERA_H
#include <stddef.h>
#include <stdint.h>
#include "psx_pose.h"

/* The original fight camera (CameraDirector 0x800633C0 and its helpers).
 * Integer state in the game's units: positions in world units (y down),
 * source angles in 18-bit units (64 per 4096-unit step), view angles in
 * 4096 units, weights in 4.12. Addresses are Japan Rev.1. */
enum {
    T3_PSX_CAMERA_SOURCES = 5,
    T3_PSX_CAMERA_FIGHT = 0,      /* the framing camera */
    T3_PSX_CAMERA_SCRIPT = 1,     /* throw presets and hit cameras */
    T3_PSX_CAMERA_STREAM = 2,     /* round intro and camera streams */
    T3_PSX_CAMERA_OUTPUT = 3,     /* the blend result */
    T3_PSX_CAMERA_TRANSITION = 4,
    T3_PSX_CAMERA_INTROS = 10,
    T3_PSX_CAMERA_PRESETS = 30,
    T3_PSX_CAMERA_HIT_ROWS = 4,
    T3_PSX_CAMERA_HIT_LISTS = 3,
    T3_PSX_CAMERA_HIT_RECORDS = 4,
    T3_PSX_CAMERA_HIT_SCRIPTS = 7,
    T3_PSX_CAMERA_SHAKES = 3,
    T3_PSX_CAMERA_SHAKE_BYTES = 96
};
/* g_cameraPhase (0x80095890). */
enum {
    T3_PSX_CAMERA_RESET = 0, T3_PSX_CAMERA_INTRO = 1, T3_PSX_CAMERA_FIGHT_PHASE = 2,
    T3_PSX_CAMERA_WINNER_0 = 3, T3_PSX_CAMERA_WINNER_1 = 4, T3_PSX_CAMERA_WINNER = 5,
    T3_PSX_CAMERA_LOSER_0 = 6, T3_PSX_CAMERA_LOSER_1 = 7
};
/* Director sub-state within the fight phase (0x800A9138). */
enum {
    T3_PSX_CINEMATIC_IDLE = 1, T3_PSX_CINEMATIC_PRESET = 2, T3_PSX_CINEMATIC_PRESET_IN = 3,
    T3_PSX_CINEMATIC_PRESET_OUT = 4, T3_PSX_CINEMATIC_TRANSITION = 5,
    T3_PSX_CINEMATIC_STREAM = 6, T3_PSX_CINEMATIC_HIT = 8
};

/* One weighted camera choice: kind 0 ends a list, 1 has no mirror twin;
 * script < -1 is a camera stream in the owner's bank section 8. */
typedef struct { int16_t kind; uint16_t weight; int32_t script; } t3_psx_camera_choice_t;

/* A camera stream of bank section 8, baked to samples (eye xyz, target xyz,
 * weight per frame) by FUN_80038F38's decoder. */
typedef struct { uint16_t offset,frames,yaw_mode; uint32_t first; } t3_psx_camera_stream_t;
typedef struct {
    const t3_psx_camera_stream_t* streams;size_t stream_count;
    const int16_t (*samples)[7];size_t sample_count;
} t3_psx_camera_streams_t;

/* A hit camera script (0x80024418, 32 bytes each): kind 0 frames the
 * fighters, 1 looks at the contact point, -1 none. */
typedef struct {
    int16_t kind,frames,blend_in,blend_out;
    int32_t start_distance,end_distance;
    int16_t pitches[4],yaws[4];
} t3_psx_camera_hit_script_t;
/* A hit camera list record: the cumulative threshold out of 0x1000
 * (FUN_80067ECC) and the script per rating (other, close, counter, KO). */
typedef struct { int16_t threshold; int16_t script[4]; } t3_psx_camera_hit_record_t;

typedef struct {
    const t3_psx_trig_t* trig;            /* 4096-entry sine, cosine at +1024 */
    const uint8_t* atan;                  /* u8[1025] octant table (0x800210C8) */
    int16_t intro_offsets[T3_PSX_CAMERA_INTROS][3];   /* 0x800988E8 */
    /* Throw presets (0x80024184): blend in, blend out, pitches[4], yaws[4]. */
    int16_t presets[T3_PSX_CAMERA_PRESETS][10];
    int16_t preset_index[T3_PSX_CAMERA_PRESETS];     /* 0x800243DC: preset + 1 per script */
    int16_t outside_blend_out;                        /* 0x80024172 */
    const t3_psx_camera_choice_t* choices;            /* EXE lists for ids < 0x2B (0x80098750) */
    size_t choice_count;
    /* Hit cameras: rows (attacker bank type, move slot, list id 300-302) at
     * 0x80024510, lists at 0x800244F8, scripts at 0x80024418. */
    int16_t hit_rows[T3_PSX_CAMERA_HIT_ROWS][3];
    t3_psx_camera_hit_record_t hit_records[T3_PSX_CAMERA_HIT_LISTS][T3_PSX_CAMERA_HIT_RECORDS];
    uint8_t hit_record_count[T3_PSX_CAMERA_HIT_LISTS];
    t3_psx_camera_hit_script_t hit_scripts[T3_PSX_CAMERA_HIT_SCRIPTS];
    /* Round end: the 384-byte square root table (as psx_body) and the
     * winner camera style B choices (0x8001DF54/5C/64). */
    const uint8_t* sqrt;
    size_t sqrt_size;
    int16_t winner_lateral[3],winner_forward[3],winner_height[3];
    /* Camera shake scripts (pointers at 0x80097EC8): pitch offsets, -128 ends. */
    int8_t shakes[T3_PSX_CAMERA_SHAKES][T3_PSX_CAMERA_SHAKE_BYTES];
} t3_psx_camera_tables_t;

/* What the director reads of one fighter, as the previous frame left it. */
typedef struct {
    int32_t pos_x,pos_z;        /* +0x00/+0x08 anchor of the running move */
    int32_t root_x,root_z;      /* +0xF68/+0xF70 displayed root */
    int32_t head_y;             /* +0x994 joint 2 world y */
    int16_t screen_x;           /* +0x1278 */
    int16_t throw_state;        /* +0x74 */
    int16_t pose_frame;         /* +0x58 */
    int16_t cur_slot;           /* +0xA0 */
    int16_t bank_type;          /* +0x16 */
    uint32_t move_flags;        /* running move row +0x24: camera byte, TRACK_ROOT bit 29 */
    uint32_t move_id;           /* identifies the running move row (the game compares pointers) */
    uint32_t move_state;        /* running move row +0x04 */
    int16_t event_left_b;       /* frames to the move's last 0x0Bxx event, -1 none */
    int16_t event_left_c;       /* frames to the move's last 0x0Cxx event, -1 none */
    int16_t transition;         /* +0x19A */
    int16_t heading;            /* +0x2C */
    int16_t facing;             /* +0x0E */
    int32_t root_y;             /* +0xF6C */
    int32_t health;             /* +0x3F4 (16.16) */
    int32_t pos_y;              /* +0x04 */
    uint8_t hit_clean,counter_hit,close_hit,ko;   /* +0xD0, +0xD3, +0xD4, +0xDE */
    int16_t last_damage;        /* +0xE8 */
    int32_t hit_point[3];       /* the best hit slot's contact point (+0x13C + 0x2C * +0x194) */
    const t3_psx_camera_choice_t* bank_choices;   /* the fighter's bank section 7 */
    size_t bank_choice_count;
    const t3_psx_camera_streams_t* bank_streams;  /* the fighter's bank section 8, baked */
} t3_psx_camera_fighter_t;

typedef struct { int32_t x,y,z,pitch,yaw,h; } t3_psx_camera_source_t;
/* Per-source framing state (0x800A06D0 + 0x30 * source). */
typedef struct {
    int16_t phase,moved,cleared_a,cleared_b,frames;
    int32_t angle,speed,pitch,pitch_target,pitch_step,error,depth_step,depth,lateral;
} t3_psx_camera_source_state_t;
/* g_camera (0x800A8A88). */
typedef struct { int32_t pitch,yaw,roll,x,y,z,h; } t3_psx_camera_view_t;

typedef struct {
    t3_psx_camera_source_t source[T3_PSX_CAMERA_SOURCES];
    t3_psx_camera_source_state_t source_state[T3_PSX_CAMERA_SOURCES];
    int16_t weight[T3_PSX_CAMERA_SOURCES],prev_weight[T3_PSX_CAMERA_SOURCES];
    int32_t target[2][4],prev_target[2][4];   /* x, y, z, screen x */
    t3_psx_camera_view_t view;
    int16_t h,h_min,h_max;
    int32_t height_term;
    int16_t state,enabled,intro_timer,cinematic;
    int16_t wait,count,count_total,throw_player,hit_count,hit_total,skip_side;
    t3_psx_camera_source_t intro_base;
    int16_t intro_length,intro_index;
    int32_t intro_counter;
    /* Throw cameras (0x800A9156-0x800A9184). */
    int16_t thrower,side,switch_mode,script_kind,choice_index,script_player,mirror;
    int32_t script;             /* preset (< 30), stream (< -1) or -1 */
    uint32_t script_move;
    int32_t preset_pitch,preset_yaw;
    /* Camera streams (0x800A917C-0x800A91A6, sample at 0x800A07D0). */
    int32_t stream;             /* the playing stream's script, -1 none */
    int16_t stream_player,stream_frames,stream_frame,stream_restart,stream_rescale;
    int32_t stream_origin[3];
    int16_t stream_sample[8];   /* eye xyz, target xyz, weight, H */
    /* Hit cameras (script at 0x800A07E0, contact point at 0x800A07F0). */
    int16_t hit_script;         /* index into the tables' hit scripts, -1 none */
    int32_t hit_point[3];
    /* Round-end cameras (0x8009EA48-0x8009EA9C, 0x800A0840-0x800A08B4). */
    int16_t winner_style,round_end_player;
    int32_t winner_a[9],winner_b[9];   /* pitch, yaw, roll (18-bit), -, -, eye xyz, - */
    int16_t winner_drift[2];
    int32_t loser[7];                  /* eye xyz, target xyz, roll */
    int32_t loser_height,loser_yaw;
    uint8_t overhead;                  /* 0x800B08D4: the overhead KO view */
    /* KO replay camera (0x800A0820-0x800A0882). */
    uint8_t replay_on,replay_reset,replay_style;
    int16_t replay_distance,replay_pitch,replay_yaw,replay_swing,replay_swing_to,replay_track;
    int32_t replay_targets[6];         /* x0 x1 y0 y1 z0 z1 */
    /* Camera shake (0x80097EC4 points into the running script). */
    int16_t shake_script,shake_pos;
    /* Bugs 1 (look point uses target 0's z for target 1) and 2 (division
     * by zero answers the R3000's -1/+1) are reproduced when set. */
    uint8_t faithful;
} t3_psx_camera_t;

/* Inputs of one director frame besides the fighters. */
typedef struct {
    int32_t fighter_distance;   /* g_fighterDistance from the previous frame */
    int32_t rounds_played;
    uint32_t* rand;             /* the game's rand() state (0x800A3E80), shared */
    uint8_t practice;           /* practice mode: no throw cameras */
    uint32_t* camera_rand;      /* the camera generator (0x800A95F4), round-end cameras */
    uint32_t* ai_rand;          /* the AI generator (0x800AE168), replay camera */
} t3_psx_camera_frame_t;

/* The view matrix CameraBuildView makes from g_camera: row 1 is scaled by
 * 0x1BD2/4096 for the display aspect, t = -R * eye. */
typedef struct {
    int16_t m[3][3];
    int32_t t[3];
    int32_t h;
} t3_psx_camera_matrix_t;

/* The panorama's turn (StageBackgroundDraw 0x8006E4A0 state at 0x800A9650). */
typedef struct {
    int32_t angle,prev_yaw,yaw,point_x,point_z;
} t3_psx_backdrop_turn_t;

int t3_psx_camera_valid(const t3_psx_camera_tables_t* tables);
/* CameraReset (0x80062CF8). */
void t3_psx_camera_reset(t3_psx_camera_t* camera);
/* The round start (FUN_8002AB68 part): sources and intro base. */
int t3_psx_camera_round_start(t3_psx_camera_t* camera,const t3_psx_camera_tables_t* tables,
                              const t3_psx_camera_fighter_t fighters[2],
                              const t3_psx_camera_frame_t* frame);
/* CameraDirector. Returns 0 on invalid input, -1 for a phase it does not know. */
int t3_psx_camera_step(t3_psx_camera_t* camera,const t3_psx_camera_tables_t* tables,
                       const t3_psx_camera_fighter_t fighters[2],
                       const t3_psx_camera_frame_t* frame,int16_t* phase);
/* FUN_80068A44: the loser camera, set up by the round flow at the result
 * before it enters phase LOSER_0/1. `alive`: a time out. */
int t3_psx_camera_loser_start(t3_psx_camera_t* camera,const t3_psx_camera_tables_t* tables,
                              const t3_psx_camera_fighter_t* loser,int alive,
                              const t3_psx_camera_frame_t* frame);
/* FUN_80069824 at a KO replay's start, and FUN_800696FC each replay frame
 * (the replay runs this instead of the director). */
int t3_psx_camera_replay_start(t3_psx_camera_t* camera,const t3_psx_camera_tables_t* tables,
                               const t3_psx_camera_fighter_t fighters[2],
                               const t3_psx_camera_frame_t* frame);
void t3_psx_camera_replay_step(t3_psx_camera_t* camera,const t3_psx_camera_tables_t* tables,
                               const t3_psx_camera_fighter_t fighters[2]);
/* CameraShakeStart (0x8004AF94), from landings and move events 1-3; the
 * pad pulse it also sends is the caller's. */
void t3_psx_camera_shake_start(t3_psx_camera_t* camera,int script);
/* CameraShakeStep (0x8004AFCC), after the director: the script's next byte
 * is added to the view pitch. */
void t3_psx_camera_shake_step(t3_psx_camera_t* camera,const t3_psx_camera_tables_t* tables);
int t3_psx_camera_build_view(const t3_psx_camera_view_t* view,
                             const t3_psx_camera_tables_t* tables,
                             t3_psx_camera_matrix_t* out);
/* RTPS of a world point through the view (FUN_80036CFC): screen x, y
 * (offset 192, 240; -0x400..0x3FF) and depth. Fighter +0x1278 is joint
 * 11's screen x. */
void t3_psx_camera_project(const t3_psx_camera_matrix_t* matrix,const int32_t point[3],int32_t out[3]);
void t3_psx_backdrop_setup(t3_psx_backdrop_turn_t* turn,const t3_psx_camera_view_t* view);
int t3_psx_backdrop_step(t3_psx_backdrop_turn_t* turn,const t3_psx_camera_view_t* view,
                         const t3_psx_camera_tables_t* tables,int intro);
#endif
