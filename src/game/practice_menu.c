/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from Tekken3Remake remake/core/game/practice_mode.gd; see practice_menu.h and NOTICE.md. */
#include "practice_menu.h"
#include "practice_replay.h"

void t3_practice_menu_reset(t3_practice_menu_t* m) {
    *m=(t3_practice_menu_t){0};
    m->attack_data=1;m->key_display=1;
    m->cpu_difficulty=1;m->cpu_level=2;
    m->mode_select=1;m->changed=1;   /* FUN_800B2D74: a session starts on MODE SELECT */
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
    /* The item tables at 0x800B0A10, 0x800B0A1C and 0x800B0A28. */
    unsigned n=0;
    rows[n++]=T3_PM_ROW_OK_CANCEL;rows[n++]=T3_PM_ROW_COMMAND_LIST;
    if(m->sub==T3_PM_SUB_VS_CPU){rows[n++]=T3_PM_ROW_CPU_DIFFICULTY;rows[n++]=T3_PM_ROW_CPU_LEVEL;}
    else if(m->sub==T3_PM_SUB_COMBO){rows[n++]=T3_PM_ROW_COMBO_PLAYER;rows[n++]=T3_PM_ROW_COMBO_TYPE;}
    else {rows[n++]=T3_PM_ROW_DUMMY;rows[n++]=T3_PM_ROW_COUNTER;}
    rows[n++]=T3_PM_ROW_ATTACK_DATA;rows[n++]=T3_PM_ROW_FREEZE_SIGNAL;rows[n++]=T3_PM_ROW_REPLAY;
    if(m->sub!=T3_PM_SUB_COMBO)rows[n++]=T3_PM_ROW_KEY_DISPLAY;   /* combo training keeps the display on */
    rows[n++]=T3_PM_ROW_RETURN;
    return n;
}

unsigned t3_practice_menu_cursor(const t3_practice_menu_t* m) {
    return m->mode_select?m->select_cursor:m->cursors[m->sub%T3_PM_SUBS];
}

/* A value row: left/right (or a choice button) change it, wrapping at `count`. */
static uint8_t cycle(uint8_t v,unsigned count,int step) {
    return (uint8_t)((v+(unsigned)((int)count+step))%count);
}

/* FUN_800B49C4: moving MODE SELECT's cursor onto a sub-mode resets it. */
static void select_sub(t3_practice_menu_t* m,unsigned sub) {
    m->sub=(uint8_t)sub;
    m->recorder=0;m->command_open=0;m->combo_type=0;m->combo_player=0;
    if(sub==T3_PM_SUB_FREE)m->attack_data=1;
    else {
        m->counter_attacks=0;m->replay_setting=0;
        if(sub==T3_PM_SUB_COMBO){m->key_display=1;m->dummy=T3_DUMMY_STAND;}
    }
}

/* One list's frame: the d-pad moves the cursor (wrapping) and adds 64 pixels a line to the offset, which then eases out by a third
 * of itself (plus a pixel) each frame. */
static void command_scroll(uint16_t* cursor,int16_t* offset,unsigned count,unsigned pressed) {
    const int d=(pressed&T3_PM_DOWN)?1:(pressed&T3_PM_UP)?-1:(pressed&T3_PM_RIGHT)?5:(pressed&T3_PM_LEFT)?-5:0;
    if(count && d) {
        int v=(int)(*cursor<count?*cursor:0u)+d;
        while(v<0)v+=(int)count;
        while(v>=(int)count)v-=(int)count;
        *cursor=(uint16_t)v;
        *offset=(int16_t)(*offset+64*d);
    }
    if(*offset) {
        int magnitude=*offset<0?-*offset:*offset;
        magnitude-=(magnitude+3)/3;
        if(magnitude<0)magnitude=0;
        *offset=(int16_t)(*offset<0?-magnitude:magnitude);
    }
}

unsigned t3_practice_menu_step(t3_practice_menu_t* m,unsigned pressed) {
    if(m->command_open) {
        /* FUN_800789E4: up and down move by one, left and right by five, wrapping, the list easing to its new place; a face
         * button or Start leaves. */
        command_scroll(&m->command_cursor,&m->command_offset,m->command_count[m->sub==T3_PM_SUB_COMBO?(m->combo_player&1u):0u],pressed);
        if(m->command_two)command_scroll(&m->command_cursor2,&m->command_offset2,m->command_count[1],m->pressed2);
        if(pressed&(T3_PM_OK|T3_PM_START))m->command_open=0;
        return T3_PM_NONE;
    }
    if(pressed&T3_PM_START){
        m->open=(uint8_t)!m->open;
        if(m->open)m->changed=1;   /* every pause starts the first row at CANCEL */
        return T3_PM_NONE;
    }
    if(!m->open)return T3_PM_NONE;
    if(m->mode_select) {
        /* FUN_800B49C4, FUN_800B4C8C: five rows, wrapping; a face button chooses. */
        const unsigned old=m->select_cursor;
        if(pressed&T3_PM_UP)m->select_cursor=(uint8_t)(old?old-1u:T3_PM_SELECT_ROWS-1u);
        if(pressed&T3_PM_DOWN)m->select_cursor=(uint8_t)(old+1u<T3_PM_SELECT_ROWS?old+1u:0u);
        if(m->select_cursor!=old && m->select_cursor<T3_PM_SUBS)select_sub(m,m->select_cursor);
        if(pressed&T3_PM_OK) {
            if(m->select_cursor==T3_PM_SELECT_PLAYER){m->open=0;return T3_PM_CHARACTER_SELECT;}
            if(m->select_cursor==T3_PM_SELECT_RESET){m->open=0;return T3_PM_LEAVE;}
            m->mode_select=0;m->changed=0;m->cursors[0]=m->cursors[1]=m->cursors[2]=0;
            if(m->sub==T3_PM_SUB_COMBO){m->combo_player=0;m->combo_type=0;m->key_display=1;}
        }
        return T3_PM_NONE;
    }
    uint8_t rows[T3_PM_MAX_ROWS];
    const unsigned count=t3_practice_menu_rows(m,rows);
    uint8_t* cursor=&m->cursors[m->sub%T3_PM_SUBS];
    /* FUN_800B4BA8: up and down wrap. */
    if(pressed&T3_PM_UP)*cursor=(uint8_t)(*cursor?*cursor-1u:count-1u);
    if(pressed&T3_PM_DOWN)*cursor=(uint8_t)(*cursor+1u<count?*cursor+1u:0u);
    /* FUN_800B4E4C: left and right change a value; a face button on a value row sends the cursor back to the first row. */
    const int step=(pressed&T3_PM_RIGHT)?1:(pressed&T3_PM_LEFT)?-1:0;
    const unsigned face=pressed&T3_PM_OK;
    const uint8_t row=rows[*cursor];
    unsigned value_row=1;
    switch(row) {
    case T3_PM_ROW_OK_CANCEL:value_row=0;if(face)m->open=0;break;
    case T3_PM_ROW_DUMMY:if(step)m->dummy=cycle(m->dummy,T3_DUMMY_SETTINGS,step),m->changed=0;break;
    case T3_PM_ROW_COUNTER:if(step)m->counter_attacks^=1u,m->changed=0;break;
    case T3_PM_ROW_ATTACK_DATA:if(step)m->attack_data^=1u,m->changed=0;break;
    case T3_PM_ROW_FREEZE_SIGNAL:if(step)m->freeze_signal^=1u,m->changed=0;break;
    case T3_PM_ROW_COMMAND_LIST:   /* a face button opens it (when the character has a list) */
        value_row=0;
        if(face && m->command_count[m->sub==T3_PM_SUB_COMBO?(m->combo_player&1u):0u]){m->command_open=1;m->command_cursor=m->command_cursor2=0;m->command_offset=m->command_offset2=0;}
        break;
    case T3_PM_ROW_REPLAY:   /* OFF, +4, +5, +6 COMBO, MANUAL (not in combo training) */
        if(step)m->replay_setting=cycle(m->replay_setting,m->sub==T3_PM_SUB_COMBO?T3_REPLAY_SETTINGS-1u:T3_REPLAY_SETTINGS,step),m->changed=0;
        break;
    case T3_PM_ROW_KEY_DISPLAY:if(step)m->key_display^=1u,m->changed=0;break;
    case T3_PM_ROW_CPU_DIFFICULTY:if(step)m->cpu_difficulty=cycle(m->cpu_difficulty,3,step),m->changed=0;break;
    case T3_PM_ROW_COMBO_PLAYER:   /* the other player's character, and the first of its combos */
        if(step){m->combo_player^=1u;m->combo_type=0;m->changed=0;}
        break;
    case T3_PM_ROW_COMBO_TYPE:     /* cycles through the character's combos */
        if(step && m->combo_count[m->combo_player&1u])m->combo_type=cycle(m->combo_type,m->combo_count[m->combo_player&1u],step),m->changed=0;
        break;
    case T3_PM_ROW_CPU_LEVEL:if(step)m->cpu_level=cycle(m->cpu_level,10,step),m->changed=0;break;
    case T3_PM_ROW_RETURN:   /* back to MODE SELECT, on the current sub-mode */
        value_row=0;
        if(face){m->mode_select=1;m->select_cursor=m->sub;}
        break;
    default:break;
    }
    if(value_row && face)*cursor=0;
    return T3_PM_NONE;
}
