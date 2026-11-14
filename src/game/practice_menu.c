/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from Tekken3Remake remake/core/game/practice_mode.gd; see practice_menu.h and NOTICE.md. */
#include "practice_menu.h"
#include "practice_replay.h"

void t3_practice_menu_reset(t3_practice_menu_t* m) {
    *m=(t3_practice_menu_t){0};
    m->attack_data=1;m->key_display=1;
    m->cpu_difficulty=1;m->cpu_level=2;
}

const char* t3_practice_dummy_name(unsigned setting) {
    static const char* const NAMES[T3_DUMMY_SETTINGS]={"STAND","CROUCH","STAND GUARD","CROUCH GUARD","GUARD ALL",
        "AUTO GUARD","UKEMI MAE","UKEMI OKU","CONTROLLER"};
    return setting<T3_DUMMY_SETTINGS?NAMES[setting]:"";
}

const char* t3_practice_cpu_level_name(unsigned level) {
    static const char* const NAMES[10]={"STAGE 1","STAGE 2","STAGE 3","STAGE 4","STAGE 5","STAGE 6","STAGE 7",
        "STAGE 8","STAGE 9","FINAL STAGE"};
    return level<10u?NAMES[level]:"";
}

const char* t3_practice_replay_name(unsigned setting) {
    static const char* const NAMES[T3_REPLAY_SETTINGS]={"OFF","+4 COMBO","+5 COMBO","+6 COMBO","MANUAL"};
    return setting<T3_REPLAY_SETTINGS?NAMES[setting]:"";
}

unsigned t3_practice_menu_rows(const t3_practice_menu_t* m,uint8_t rows[T3_PM_MAX_ROWS]) {
    unsigned n=0;
    rows[n++]=T3_PM_ROW_MODE;
    if(m->sub==T3_PM_SUB_VS_CPU){rows[n++]=T3_PM_ROW_CPU_DIFFICULTY;rows[n++]=T3_PM_ROW_CPU_LEVEL;}
    else if(m->sub==T3_PM_SUB_COMBO){rows[n++]=T3_PM_ROW_COMBO_PLAYER;rows[n++]=T3_PM_ROW_COMBO_TYPE;}
    else {rows[n++]=T3_PM_ROW_DUMMY;rows[n++]=T3_PM_ROW_COUNTER;}
    rows[n++]=T3_PM_ROW_ATTACK_DATA;rows[n++]=T3_PM_ROW_FREEZE_SIGNAL;rows[n++]=T3_PM_ROW_REPLAY;
    if(m->sub!=T3_PM_SUB_COMBO)rows[n++]=T3_PM_ROW_KEY_DISPLAY;   /* combo training keeps the display on */
    rows[n++]=T3_PM_ROW_SELECT;rows[n++]=T3_PM_ROW_EXIT;
    return n;
}

/* A value row: left/right (or a choice button) change it, wrapping at `count`. */
static uint8_t cycle(uint8_t v,unsigned count,int step) {
    return (uint8_t)((v+(unsigned)((int)count+step))%count);
}

unsigned t3_practice_menu_step(t3_practice_menu_t* m,unsigned pressed) {
    if(pressed&T3_PM_START){m->open=(uint8_t)!m->open;return T3_PM_NONE;}
    if(!m->open)return T3_PM_NONE;
    uint8_t rows[T3_PM_MAX_ROWS];
    const unsigned count=t3_practice_menu_rows(m,rows);
    /* FUN_800B4BA8: up and down wrap. */
    if(pressed&T3_PM_UP)m->cursor=(uint8_t)(m->cursor?m->cursor-1u:count-1u);
    if(pressed&T3_PM_DOWN)m->cursor=(uint8_t)(m->cursor+1u<count?m->cursor+1u:0u);
    const int step=(pressed&T3_PM_RIGHT)?1:(pressed&T3_PM_LEFT)?-1:(pressed&T3_PM_OK)?1:0;
    switch(rows[m->cursor]) {
    case T3_PM_ROW_MODE:
        if(step) {
            /* Moving to another sub-mode resets it: FREE turns ATTACK DATA on; VS CPU clears COUNTER ATTACKS. */
            m->sub=cycle(m->sub,T3_PM_SUBS,step);
            if(m->sub==T3_PM_SUB_FREE)m->attack_data=1;
            else {m->counter_attacks=0;m->dummy=T3_DUMMY_STAND;m->replay_setting=0;}
            if(m->sub==T3_PM_SUB_COMBO){m->key_display=1;m->combo_type=0;}
        }
        break;
    case T3_PM_ROW_DUMMY:if(step)m->dummy=cycle(m->dummy,T3_DUMMY_SETTINGS,step);break;
    case T3_PM_ROW_COUNTER:if(step)m->counter_attacks^=1u;break;
    case T3_PM_ROW_ATTACK_DATA:if(step)m->attack_data^=1u;break;
    case T3_PM_ROW_FREEZE_SIGNAL:if(step)m->freeze_signal^=1u;break;
    case T3_PM_ROW_REPLAY:   /* OFF, +4, +5, +6 COMBO, MANUAL (not in combo training) */
        if(step)m->replay_setting=cycle(m->replay_setting,m->sub==T3_PM_SUB_COMBO?T3_REPLAY_SETTINGS-1u:T3_REPLAY_SETTINGS,step);
        break;
    case T3_PM_ROW_KEY_DISPLAY:if(step)m->key_display^=1u;break;
    case T3_PM_ROW_CPU_DIFFICULTY:if(step)m->cpu_difficulty=cycle(m->cpu_difficulty,3,step);break;
    case T3_PM_ROW_COMBO_PLAYER:   /* the other player's character, and the first of its combos */
        if(step){m->combo_player^=1u;m->combo_type=0;}
        break;
    case T3_PM_ROW_COMBO_TYPE:     /* cycles through the character's combos */
        if(step && m->combo_count[m->combo_player&1u])m->combo_type=cycle(m->combo_type,m->combo_count[m->combo_player&1u],step);
        break;
    case T3_PM_ROW_CPU_LEVEL:if(step)m->cpu_level=cycle(m->cpu_level,10,step);break;
    case T3_PM_ROW_SELECT:if(pressed&T3_PM_OK){m->open=0;return T3_PM_CHARACTER_SELECT;}break;
    case T3_PM_ROW_EXIT:if(pressed&T3_PM_OK){m->open=0;return T3_PM_LEAVE;}break;
    default:break;
    }
    return T3_PM_NONE;
}
