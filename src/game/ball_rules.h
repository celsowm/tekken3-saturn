/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of Tekken Ball's ball (volley.ovl FUN_800B16A4, FUN_800B2F14, FUN_800B0B54, FUN_800B0FDC
 * and FUN_800B0C90) as ported in the pinned Tekken3Remake (tools/research/ball_sim.py,
 * docs/research/code/modes.md). Changes: caller-owned state in int32 fields instead of guest RAM; the
 * fighters' attack segments, hurt cylinders and move facts are inputs; every call into the engine (sound,
 * vibration, camera shake, replay event, popup, burn, the stand-in attacker) is an event the caller
 * performs. The original copies the hitter's whole record into the stand-in record (0x800AC808) and later
 * reads parts of it back; here the copy is the event BALL_EV_STAND_IN (the caller copies the record) and the
 * words the ball itself reads or sets are in `t3_ball_world_t.stand_in`. */
#ifndef T3_BALL_RULES_H
#define T3_BALL_RULES_H
#include <stdint.h>
#include "psx_body.h"
#include "psx_exact_contact.h"

/* Ball object states (+0x00). */
enum { BALL_SERVE_WAIT=0, BALL_WAIT=1, BALL_FLY=2, BALL_SET=3, BALL_SPIKE=4, BALL_DEAD=5, BALL_RESET=6,
       BALL_POINT=7, BALL_FIRST_WAIT=8, BALL_SERVE=9 };

/* The ball object (0x800AE23C). Every field is an int32 holding the value of the original field with its
 * width and signedness (s16 fields are kept sign-extended, u8 fields 0..255). */
typedef struct {
    int32_t state;                 /* +0x00 */
    int32_t pos[3];                /* +0x68 */
    int32_t vel[3];                /* +0x78, 8.8 */
    int32_t gravity;               /* +0x8C */
    int32_t spin[3];               /* +0x98: three s16 angles */
    int32_t spin_rate[3];          /* +0xA0 */
    int32_t speed;                 /* +0xA8, 8.8 along `angle` */
    int32_t sweep;                 /* +0xAC */
    int32_t angle, homing;         /* +0xB0, +0xB2 */
    int32_t prev[3];               /* +0xB8: the position the touch tests start from */
    int32_t touch_wait;            /* +0xCC: frames the ball ignores touches (5 after a hit) */
    int32_t squash_hold;           /* +0xCD */
    int32_t volume[4][6];          /* +0xD0: the four swept hit points, previous then current */
    int32_t owner, target, spike_target; /* +0x130, +0x134, +0x138: fighter indices */
    int32_t power[2];              /* +0x13C: each player's stored power */
    int32_t charge;                /* +0x140 */
    int32_t spike_speed;           /* +0x142 */
    int32_t rehit_count, rehit;    /* +0x144, +0x145 */
    int32_t kind;                  /* +0x146: the render kind */
    int32_t timer;                 /* +0x147 */
    int32_t squash_axis[3];        /* +0x148 */
    int32_t squash;                /* +0x150 */
} t3_ball_t;

/* The words of the engine the ball reads and writes (volley.ovl's data and the fight's globals). */
typedef struct {
    int32_t speed_unit, spike_unit, gravity_unit, neutral, gain; /* 0x800B6AD8..: the ball type's constants */
    int32_t idle;                  /* 0x800B6AE4: frames the ball has lain on the floor */
    int32_t touch_sound_wait;      /* 0x800B6AE8 */
    int32_t point;                 /* 0x800B6B4C: 1 + the index of the player who lost the point, else 0 */
    int32_t charged_hit;           /* 0x800B6B54 */
    int32_t result;                /* 0x800AFF8C: points still needed */
    int32_t no_score;              /* 0x800958A0 */
    int32_t no_winner_mark;        /* 0x800958C4 */
    int32_t round_clock;           /* 0x80095884 */
    int32_t lob_base;              /* 0x800AFF04 */
    int32_t round_state;           /* 0x80097350 */
    int32_t option;                /* 0x800AFF91: the ball type 0..2 */
    int32_t first_round;           /* 0x800958A8 == 1 */
    int32_t serve_side;            /* 0x800AFF90 */
    int32_t glows;                 /* 0x800B645C */
    /* The stand-in attacker (0x800AC808): what the ball writes into it and reads back. */
    struct {
        int32_t source;            /* the fighter its record was last copied from (-1: none) */
        int32_t damage, level;     /* +0x5E, +0x64 */
        int32_t hit83[2], hit88, hitD1, hit23;
        int32_t move_byte;         /* its move's byte +0x2C (an input) */
        int32_t x, z;              /* +0xF68, +0xF70 */
    } stand_in;
    int32_t hit_offsets[4][3];     /* 0x800B6808: the ball's four hit points relative to its centre */
} t3_ball_world_t;

/* One fighter's facts the ball needs (the fight keeps the originals). */
typedef struct {
    int32_t index, player;         /* +0x1E, +0x12 */
    int32_t x, z, head;            /* the root position pointed at by +0xF68 (+0, +8), and +0x950 */
    int32_t health;                /* +0x3F4, 16.16 */
    int32_t kind;                  /* +0x16: the character bank type (4: Yoshimitsu's) */
    int32_t level, damage, guard;  /* +0x64, +0x5E, +0x66 */
    int32_t slot;                  /* +0xA0: the running move slot */
    int32_t locked, f88, fd1, f23; /* +0x85, +0x88, +0xD1, +0x23 */
    int32_t hit83[2];              /* +0x83, +0x84 */
    int32_t move_byte;             /* byte +0x2C of the running move (the stand-in inherits it) */
    int32_t airborne;              /* +0xDB */
    int32_t busy3, busy4;          /* +0xC3, +0xC4 */
    int32_t projectile;            /* the move's segments are a projectile's (move byte +0x28 >= 0x18) */
    int32_t seg_count;             /* the segments to test (at most 4) */
    int32_t segs[4][6];            /* start, end */
    int32_t cyl[14][5];            /* hurt cylinders: centre, radius, radius squared */
    int32_t opponent_flag;         /* +0x22 */
    int32_t contact_cyl, contact_end[3], contact_dir[3]; /* +0x154, +0x13C, +0x14C: set by a body touch */
    /* What a charged touch writes into the victim of the hit, the hitter in the original's words. */
    int32_t f87, fce, fee, f15d, f15c, f104;
    int32_t drift[5];              /* the move tuning record (0x800B6B40 + 6 * player): s16 angle offset, four bytes */
} t3_ball_fighter_t;

enum { BALL_EV_SOUND=1, BALL_EV_VIBRATE, BALL_EV_SHAKE, BALL_EV_REPLAY, BALL_EV_POPUP, BALL_EV_BURN,
       BALL_EV_STAND_IN, BALL_EV_GLOW, BALL_EV_MAX=16 };
/* SOUND: a=id. VIBRATE: a=player, b=kind. SHAKE: a=kind. REPLAY: a=player, b=kind (0x82: a lethal hit).
 * POPUP: a=damage number (-1: "!"), b=colour, c..e=ball position. BURN: a=player. STAND_IN: a=source.
 * GLOW: a=player, b=size (0 small, 1 for a charge of 100 or more). */
typedef struct { int32_t kind,a,b,c,d,e; } t3_ball_event_t;
typedef struct { t3_ball_event_t e[BALL_EV_MAX]; int32_t count; } t3_ball_events_t;

typedef struct {
    t3_psx_body_tables_t tables;   /* the trig and atan tables */
} t3_ball_tables_t;

/* The executable's helpers the ball uses, for the CPU's scene projection: FUN_8004B634 (the angle of (x, z) in 4096 units),
 * the sine and cosine tables (4.12, the angle masked to 12 bits) and ISqrt (0x8002A578). */
int32_t t3_ball_atan2(const t3_ball_tables_t* t,int32_t x,int32_t z);
int32_t t3_ball_sin(const t3_ball_tables_t* t,int32_t angle);
int32_t t3_ball_cos(const t3_ball_tables_t* t,int32_t angle);
uint32_t t3_ball_isqrt(uint32_t n);
/* FUN_800B16A4: one frame of the ball. `s7` is the caller's $s7 (see ball_sim.ball_update). */
int t3_ball_update(t3_ball_t* ball,t3_ball_world_t* world,t3_ball_fighter_t f[2],const t3_ball_tables_t* t,
                   int32_t s7,t3_ball_events_t* ev);
/* FUN_800B2F14: the squash and the render kind. */
int t3_ball_after_update(t3_ball_t* ball,t3_ball_world_t* world,const t3_ball_tables_t* t,t3_ball_events_t* ev);
/* FUN_800B0B54: the move tuning of the running move slot of fighter f. */
void t3_ball_move_tuning(t3_ball_fighter_t* f);
/* FUN_800B0FDC (the physical part): the ball at the start of a round. */
void t3_ball_init(t3_ball_t* ball,const t3_ball_world_t* world);
/* FUN_800B0C90 (the physical part): the ball type's constants from the 24-byte option table entry
 * (0x800B6460 + 24 * option) as twelve u16 words: spike unit at byte 0, speed unit at 4, gravity at 8,
 * gain at 0x10 and neutral zone at 0x14. */
void t3_ball_fight_start(t3_ball_t* ball,t3_ball_world_t* world,const uint16_t option_row[12]);
#endif
