/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Practice mode's attack data (practice.ovl FUN_800B3808's per-player part), adapted from Tekken3Remake
 * remake/core/game/practice_mode.gd (_player_data, _throw_flag, _count_combo, _combo_markers, _damage)
 * and docs/research/code/modes.md#practice; see NOTICE.md. */
#ifndef T3_PRACTICE_DATA_H
#define T3_PRACTICE_DATA_H
#include <stdint.h>

/* What the rules read of one fighter record after its frame (the original's record fields in comments). */
typedef struct {
    uint8_t hit_clean,counter_hit,close_hit,got_hit;  /* +0xCC, +0xD3, +0xD0, +0xCE */
    uint8_t was_hit_this_move,in_reaction;            /* +0x?? the move was hit, +0xC8 */
    uint8_t move_changed,whiffed;                      /* +0xDA, the opponent's whiff */
    int16_t last_extra_damage,throw_state;             /* +0xE8's extra term, +0xC4's state */
    int16_t damage,base_damage;                        /* the best hit slot's damage after and before scaling */
    uint32_t move_flags,pose_flags;                    /* the move started this frame, the one that ran before */
    uint16_t cur_slot,attack;                          /* the move's attack word (+0x64) */
    uint8_t bank_type;
    uint8_t step_transition,thrown_transition;         /* the move's transition is STEP / STEP_CROUCH, THROWN */
    int16_t react_chain;                               /* +0x80, from t3_practice_react_chain (MoveStep owns it) */
} t3_practice_facts_t;

/* Per-player data, practice S+0x14 + 0x20 * player. */
typedef struct {
    int32_t total,combo_damage,shown_damage;
    int16_t hit_damage,hit_share,marker,hits;
    uint8_t combo,new_best,ended,previous,dropped,counter,clean,best;
    int8_t hit_id;
    uint8_t throw_now,throw_before;
} t3_practice_player_t;
typedef struct { t3_practice_player_t p[2]; } t3_practice_data_t;

enum { T3_PRACTICE_BANK_KING=3,T3_PRACTICE_BANK_YOSHIMITSU=4,T3_PRACTICE_BANK_XIAOYU=7,T3_PRACTICE_BANK_BRYAN=12 };
enum { T3_PRACTICE_FLAG_BUFFER_INPUT=0x2000u,T3_PRACTICE_FLAG_REACT_CHAIN=0x40000u };

/* move_system.gd _count_react_chain: the next value of a fighter's +0x80 after its move step. */
int16_t t3_practice_react_chain(int16_t chain,const t3_practice_facts_t* f);
/* The hit markers (FUN_800B3808, practice_mode.gd _attack_data): four, each the attacker's attack word and the
 * frames left; a clean hit on the dummy takes the next one for 60 frames. */
enum { T3_PRACTICE_MARKERS=4,T3_PRACTICE_MARKER_FRAMES=0x3C };
typedef struct {
    int16_t attack[T3_PRACTICE_MARKERS],frames[T3_PRACTICE_MARKERS];
    uint8_t index;
    int32_t point[T3_PRACTICE_MARKERS][3];   /* the hit's world point (the attack segment's end at contact), for the presentation */
} t3_practice_markers_t;
/* A marker's picture by its attack word (practice_sim.py hit_marker's kind): 0 high, 1 mid, 2 low, 3 unblockable, 4 none. */
unsigned t3_practice_marker_kind(unsigned attack_word);
/* After a step that took a marker: where it hit. */
void t3_practice_markers_point(t3_practice_markers_t* m,const int32_t point[3]);
void t3_practice_markers_step(t3_practice_markers_t* m,uint16_t player_attack,int dummy_hit_clean);
void t3_practice_data_reset(t3_practice_data_t* d);
/* Once per fight frame, after both fighters' hits: `me` is player 0's record, `dummy` player 1's. */
void t3_practice_data_step(t3_practice_data_t* d,const t3_practice_facts_t facts[2]);
#endif
