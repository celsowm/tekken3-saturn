/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Tekken Ball's part of the fight frame (volley.ovl FUN_800B0E14, "after MoveBranchAll") on the owned match: the
 * fighters' facts for ball_rules.c are read from the match, and what the ball decides (health lost on a point or a
 * charged hit, the lock that stops one attack hitting the ball twice) is written back. The ball state is the
 * caller's, not part of the match, so the versus program is unchanged. See docs/TEKKEN_BALL_PLAN.md. */
#ifndef T3_BALL_GAME_H
#define T3_BALL_GAME_H
#include "ball_rules.h"
#include "ball_hud.h"
#include "psx_match.h"

enum { BALL_DATA_VERSION=1, BALL_COURT_HALF_X=0x1400, BALL_COURT_HALF_Z=0x100 };

/* A text over the ball: the charge a hit added (number) or "!" (-1), at the ball's place when it was made, for BALL_POPUP_FRAMES. */
enum { BALL_POPUPS=4, BALL_POPUP_FRAMES=30 };
typedef struct { int32_t pos[3];int16_t number;uint8_t age,colour,used; } t3_ball_popup_t;

typedef struct {
    t3_ball_t ball;
    t3_ball_world_t world;
    t3_ball_fighter_t f[2];
    t3_ball_gauge_t gauge[2];
    t3_ball_events_t events;       /* the events of the last tick (cleared at its start) */
    uint16_t options[3][12];       /* the three ball types' rows (0x800B6460) */
    int16_t last_slot[2],last_pose[2]; /* the running move, to release the per-move lock when it changes */
    uint8_t active,option,mesh_ok,last_kind;
    uint32_t ticks;
    t3_ball_popup_t popup[BALL_POPUPS];   /* the texts over the ball (volley.ovl FUN_800B445C: 8 slots there) */
    t3_psx_fighter_t stand_in;            /* the stand-in attacker (0x800AC808): the hitter's record when the ball was struck */
} t3_ball_game_t;

/* The data blob of scripts/extract_ball_data.py ("T3BL", big endian). The ball type is 0..2 (beach, gum, iron). */
int t3_ball_game_init(t3_ball_game_t* g,const uint8_t* blob,size_t size,unsigned option,unsigned serve_side);
/* The mesh, normals, faces and colours of the blob for the renderer: pointers into it, or 0 if it is not a ball blob. */
typedef struct {
    const uint8_t *vertices,*normals,*triangles,*quads,*colours; /* big endian s16[3], s16[3], u8[3], u8[4], u32[16] */
    const uint8_t* style;                                         /* the type's option row: 12 big-endian u16 (0x800B6460 + 24 * type) */
} t3_ball_mesh_t;
int t3_ball_mesh(const uint8_t* blob,size_t size,unsigned option,t3_ball_mesh_t* out);
/* One tick of the ball after the match stepped: facts in, the ball runs, health and locks out. Returns 0 on bad input. */
int t3_ball_game_tick(t3_ball_game_t* g,t3_psx_match_t* match,const t3_ball_tables_t* tables);
/* What the renderer needs of one image: the ball, the gauges and the fighters' depth (the court is centred between them). */
typedef struct {
    t3_ball_t ball;
    t3_ball_gauge_t gauge[2];
    int32_t fighter_z[2];
    int32_t round_state;           /* the round flow's state (T3_ROUND_*), for the backdrop's start offset */
    t3_ball_popup_t popup[BALL_POPUPS];
    uint8_t option,active;
} t3_ball_view_t;
void t3_ball_game_view(const t3_ball_game_t* g,t3_ball_view_t* out);
/* The gauge fills' maximum power (the original draws power * 76 / 100). */
enum { BALL_POWER_MAX=100 };
#endif
