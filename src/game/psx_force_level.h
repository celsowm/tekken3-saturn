/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of the level runner of Tekken Force (force.ovl FUN_800B1778, Japan Rev.1) as ported
 * and documented in the pinned Tekken3Remake (tools/research/force_sim.py, docs/research/code/modes.md).
 * Changes: the original's globals and fighter fields are one caller-owned state; hooks into the fight
 * engine become an event list; drawing and projection are the caller's. */
#ifndef T3_PSX_FORCE_LEVEL_H
#define T3_PSX_FORCE_LEVEL_H
#include <stddef.h>
#include <stdint.h>

#define T3_FORCE_SLOTS 2
#define T3_FORCE_EVENTS 16
#define T3_FORCE_SCRIPT_RECORDS 128
#define T3_FORCE_SCRIPT_BYTES (T3_FORCE_SCRIPT_RECORDS*20)
/* The constant tables at the start of force.ovl (build_force_tables.py, TABLES.BIN), by offset. */
#define T3_FORCE_TABLE_BYTES 0x1D0u
#define T3_FORCE_VECTOR_WORDS 115
#define T3_FORCE_ITEMS 2

/* What the level runner asks of its caller, in call order. a, b, c as the original's arguments. */
enum {
    T3_FORCE_EVENT_MUSIC_FADE=1,   /* a: 0 */
    T3_FORCE_EVENT_START_MOVE,     /* a: enemy slot, b: move slot */
    T3_FORCE_EVENT_ENEMY_ACTION,   /* a, b, c */
    T3_FORCE_EVENT_SOUND,          /* a: sound id */
    T3_FORCE_EVENT_MODEL_RESET,    /* a: enemy slot */
    T3_FORCE_EVENT_MODEL_REQUEST,  /* a: fighter index, b: costume + 1 */
    T3_FORCE_EVENT_MODEL_BIND,     /* a: enemy slot */
    T3_FORCE_EVENT_SAVE_PENDING,
    T3_FORCE_EVENT_AREA_CHANGE,
    T3_FORCE_EVENT_HEALTH_FLASH    /* the player's back colour flash (FUN_8003A388) */
};
typedef struct { int32_t kind,a,b,c; } t3_force_event_t;

/* One enemy slot (24 bytes at 0x800B6A7C in the original). State: 0 release, 1 free, 2-3 load the
 * costume's model, 4 enter with full health, 5 fight, 6 defeated (+n SEC), 7 blink out. */
typedef struct {
    int16_t state;
    uint16_t timer;
    int16_t type;       /* 0..4 */
    int16_t loaded;     /* costume of the loaded model */
    uint16_t requested; /* costume to load */
    uint16_t popup_x,popup_y,popup_until; /* the "+n SEC" popup, y in 1/16 dots */
} t3_force_slot_t;

/* The fields of an enemy's fighter record the runner reads or writes (original offsets). */
typedef struct {
    int32_t health,health_max;    /* +0x3F4, +0x3F8 */
    int32_t f04;
    uint16_t f92,key;             /* +0x92, +0x14 costume key */
    uint8_t fb8,fc2,fc3,fc4,fce,fe7,owner,index; /* +0xB8, +0xC2 (released), +0xC3 (shown), +0xC4, +0xCE, +0xE7, +0x22, +0x1E */
    int32_t anchor_x,anchor_z,root_x,root_z; /* +0, +8, +0xF68, +0xF70 */
    int32_t screen_x,screen_y;    /* input: the enemy's projected position for the popup */
} t3_force_fighter_t;

/* An item slot (0xBC bytes at 0x800B6E90 in the original): state 0 free, 1 on the ground, 2 LIFE UP! rising. */
typedef struct {
    uint32_t state;
    int32_t x,z;
    uint16_t popup_x,popup_y; /* y in 1/16 dots */
    int32_t until;
} t3_force_item_t;

/* The player's fields the items read and write. */
typedef struct {
    int32_t x,z;               /* +0xF68, +0xF70 */
    uint8_t db;                /* +0xDB: no pickup while set */
    int32_t screen_x,screen_z; /* input: the player's projected position for the popup */
} t3_force_player_t;

typedef struct {
    int32_t level,state,clock,drawn,tally,tally_done;
    uint32_t score,hiscore_run;
    int32_t timer,fight_start_timer,keys,key_earned,final_challenge,wait_defeated,wait_count;
    int32_t pattern_on,pattern_stop_x;
    int32_t pattern_ptr;  /* byte offset into the tables */
    int32_t script_ptr;   /* record index */
    int32_t scroll_mode,scroll_param,clear_time,bonus_time,clear_bonus;
    /* inputs from the fight */
    int32_t frozen,text_off,round_state,camera_x,origin,player_health,dr_b,player_index,lock_x;
    /* shared with the screens */
    uint8_t mode_flags,sub_mode;
    int32_t flag_70e8;
    uint8_t key_count;
    uint16_t kills;
    uint8_t hiscore_flag;
    int32_t bound[4],free_scroll;
    t3_force_slot_t slot[T3_FORCE_SLOTS];
    t3_force_fighter_t fighter[T3_FORCE_SLOTS];
    t3_force_item_t item[T3_FORCE_ITEMS];
    t3_force_player_t player;
    uint16_t countdown_sound[5]; /* sounds at 5..1 seconds left (0x8009747E in the executable) */
    t3_force_event_t events[T3_FORCE_EVENTS];
    unsigned event_count;
} t3_force_level_t;

/* One frame of the level runner (the script, the two enemy slots, the score, hi-score and timer caps and
 * the announcer's countdown). `tables` is T3_FORCE_TABLE_BYTES, `script` the level's 128 20-byte records
 * (little-endian, as in the stage archive). Events are appended to s->events (cleared first). */
void t3_force_level_step(t3_force_level_t* s,const uint8_t* tables,const uint8_t* script);

/* The flat vector of the golden cases: the state's fields in the order of tests/test_psx_force_level.c. */
void t3_force_level_load(t3_force_level_t* s,const int32_t* v);
void t3_force_level_save(const t3_force_level_t* s,int32_t* v);
#endif
