/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from the pinned Tekken3Remake (practice_sim.py, practice_view.gd); see practice_hud.h and NOTICE.md. */
#include "practice_hud.h"
#include "practice_font.h"
#include "practice_menu.h"
#include "psx_viewport.h"
#include "saturn/color.h"
#include "saturn/vdp1.h"

/* A decimal number into `out`: at least `width` digits (zero or space padded), with a sign. */
static void number(char* out,int value,unsigned width,int zero) {
    char digits[12];
    unsigned n=0;
    unsigned magnitude=(unsigned)(value<0?-value:value);
    do {digits[n++]=(char)('0'+magnitude%10u);magnitude/=10u;} while(magnitude);
    unsigned at=0;
    if(value<0)out[at++]='-';
    for(unsigned pad=n+(value<0?1u:0u);pad<width;++pad)out[at++]=zero?'0':' ';
    while(n)out[at++]=digits[--n];
    out[at]=0;
}

static void append(char* out,const char* text) {
    while(*out)++out;
    while((*out++=*text++)) {}
}

/* FUN_800B6F58: the big combo and damage counters. */
static void combo_counter(unsigned count,int damage) {
    char text[12];
    if(count) {
        number(text,(int)count,0,0);
        t3_practice_font_text(1,6,text,0xC,0x88);
        t3_practice_font_text(1,5,"COMBO",count>=10u?0x2A:0x20,0x88);
    }
    number(text,damage,0,0);
    t3_practice_font_text(1,6,text,0x16,0xA6);
    const int magnitude=damage<0?-damage:damage;
    int x=magnitude>=100?0x40:magnitude<10?0x2A:0x35;
    if(damage<0)x+=0xD;
    t3_practice_font_text(0,7,"DAMAGE",x,0xB0);
}

int t3_practice_hud_draw(const t3_practice_hud_t* hud) {
    if(!t3_practice_font_ready())return 0;
    const unsigned adv=t3_practice_font_advance(0),line=t3_practice_font_line(0);
    const t3_practice_state_t* st=hud->state;
    if(hud->attack_data) {
        const t3_practice_player_t* me=&st->data.p[0];
        const t3_practice_player_t* d=&st->data.p[1];
        char text[28];
        number(text,me->total,0,0);
        char label[28]="TOTAL DMG:";
        append(label,text);
        t3_practice_font_text(0,1,label,(int)(23u*adv),(int)(3u*line));
        number(text,d->total,0,0);
        label[10]=0;append(label,text);
        t3_practice_font_text(0,1,label,(int)(1u*adv),(int)(3u*line));
        const int hits=(int8_t)d->best;
        if(hits>=2 && d->marker && d->shown_damage>0)combo_counter((unsigned)hits,d->shown_damage);
        else if(d->hits>=2)combo_counter(0u,d->shown_damage);
        const int dmg=d->hit_share,top=d->hit_damage;
        const unsigned colour=top<dmg?7u:top==dmg?6u:3u;
        const int pct=(top && dmg)?dmg*100/top:0;
        char dmg_text[8],pct_text[8],row[28]="DMG:";
        number(dmg_text,dmg,3,1);number(pct_text,pct,3,1);
        append(row,dmg_text);append(row,"(");append(row,pct_text);append(row,"%)");
        t3_practice_font_text(0,colour,row,(int)(1u*adv),(int)(4u*line));
        if(d->counter)t3_practice_font_text(0,2,"COUNTER",(int)(1u*adv),(int)(5u*line));
        if(d->clean)t3_practice_font_text(0,5,"CLEAN HIT",(int)(1u*adv),(int)(6u*line));
    }
    /* The prompts, line 21 (modes.md "Practice HUD"). */
    const char* prompt=0;unsigned colour=1;
    if(hud->sub==T3_PM_SUB_COMBO || (hud->sub==T3_PM_SUB_FREE && hud->recorder)) {
        if(hud->sub==T3_PM_SUB_COMBO && !hud->combo_known){prompt=0;}
        else switch(st->guide.state) {
            case T3_GUIDE_READY:
                if(hud->sub==T3_PM_SUB_COMBO){prompt="PLAY = SELECT";colour=1;}
                else {prompt="PLAY(SEL) REC(DW+SEL)";colour=7;}
                break;
            case T3_GUIDE_RECORD_WAIT:prompt="START REC = ANY KEY";colour=1;break;
            case T3_GUIDE_RECORDING:prompt="REC (STOP = SEL)";colour=2;break;
            case T3_GUIDE_PLAY:prompt="PLAY";colour=5;break;
            case T3_GUIDE_MODE:prompt="GUIDE MODE";colour=6;break;
            default:break;
        }
    } else if(hud->replay_setting==T3_REPLAY_MANUAL && hud->sub==T3_PM_SUB_FREE){prompt="REPLAY = SELECT";colour=1;}
    if(prompt)t3_practice_font_text(0,colour,prompt,(int)(1u*adv),(int)(21u*line));
    return 1;
}

/* ---- the pause menu and the command list (FUN_800B57AC pages, FUN_800B6DA0 backdrop; modes.md "Pause menu drawing") ---- */
static void panel(int x,int y,int w,int h,unsigned rgb555) {
    const int sx=t3_psx_viewport_x(x),sy=t3_psx_viewport_y(y);
    (void)sat_draw_rect_screen((int16_t)sx,(int16_t)sy,(uint16_t)(t3_psx_viewport_x(x+w)-sx),(uint16_t)(t3_psx_viewport_y(y+h)-sy),
                               (uint16_t)(0x8000u|rgb555));
}

/* The dark blue panel (0, 16, 48) in its light grey frame: MODE SELECT's 172 x 192 at (8, 40), a page's 240 x 316 at (64, 40). */
static void backdrop(int mode_select) {
    if(mode_select) {
        panel(7,38,174,196,SAT_RGB555(28,28,28));
        panel(8,40,172,192,SAT_RGB555(0,2,6));
    } else {
        panel(63,38,242,320,SAT_RGB555(28,28,28));
        panel(64,40,240,316,SAT_RGB555(0,2,6));
    }
}

/* A value right-aligned to column 25 from the menu's left edge. */
static void value_text(unsigned colour,const char* text,int y) {
    unsigned length=0;
    while(text[length])++length;
    t3_practice_font_text(0,colour,text,72+(int)(length<25u?25u-length:0u)*9,y);
}

int t3_practice_menu_draw(const t3_practice_menu_view_t* view) {
    if(!t3_practice_font_ready())return 0;
    const t3_practice_menu_t* m=view->menu;
    backdrop(m->mode_select);
    if(m->command_open) {
        t3_practice_font_text(1,10,"COMMAND LIST",72,56);
        for(unsigned row=0;row<view->command_rows;++row) {
            const char *name,*command;
            if(!view->command_line(view->context,row,&name,&command))break;
            const unsigned colour=row==view->command_cursor_row?10u:11u;
            const int y=110+(int)row*22;
            t3_practice_font_text(0,colour,name,72,y);
            t3_practice_font_text(0,colour,command,72+81,y);
        }
        return 1;
    }
    if(m->mode_select) {
        /* FUN_800B5834 */
        static const char* const LABELS[T3_PM_SELECT_ROWS]={"FREE","VS CPU","COMBO TRAINING","PLAYER SELECT","RESET"};
        t3_practice_font_text(1,10,"MODE SELECT",16,56);
        for(unsigned r=0;r<T3_PM_SELECT_ROWS;++r)
            t3_practice_font_text(0,r==m->select_cursor?10u:11u,LABELS[r],16,110+(int)r*22);
        return 1;
    }
    /* FUN_800B5978, FUN_800B5ED4, FUN_800B6368: the page; row `r` of the item list keeps the colour of the cursor. */
    static const char* const DIFFICULTY[3]={"EASY","MEDIUM","HARD"};
    static const char* const OFF_ON[2]={"OFF","ON"};
    static const char* const TITLES[T3_PM_SUBS]={"FREE","VS.CPU","COMBO TRAINING"};
    uint8_t rows[T3_PM_MAX_ROWS];
    const unsigned count=t3_practice_menu_rows(m,rows);
    const unsigned cursor=m->cursors[m->sub%T3_PM_SUBS];
    t3_practice_font_text(1,10,TITLES[m->sub%T3_PM_SUBS],72,56);
    int y=110;
    for(unsigned r=0;r<count;++r) {
        const unsigned colour=r==cursor?10u:11u;
        const char* label="";const char* value="";
        char number[12]={0};
        const unsigned row=rows[r];
        switch(row) {
        case T3_PM_ROW_OK_CANCEL:label=m->changed?"CANCEL":"OK";break;
        case T3_PM_ROW_COMMAND_LIST:label="COMMAND LIST";break;
        case T3_PM_ROW_DUMMY:label="TRAINING DUMMY";value=t3_practice_dummy_name(m->dummy);break;
        case T3_PM_ROW_COUNTER:label="COUNTER ATTACKS";value=OFF_ON[m->counter_attacks&1u];break;
        case T3_PM_ROW_CPU_DIFFICULTY:label="CPU DIFFICULTY";value=DIFFICULTY[m->cpu_difficulty%3u];break;
        case T3_PM_ROW_CPU_LEVEL:label="CPU LEVEL";value=t3_practice_cpu_level_name(m->cpu_level);break;
        case T3_PM_ROW_ATTACK_DATA:label="ATTACK DATA";value=OFF_ON[m->attack_data&1u];break;
        case T3_PM_ROW_FREEZE_SIGNAL:label="FREEZE SIGNAL";value=OFF_ON[m->freeze_signal&1u];break;
        case T3_PM_ROW_REPLAY:label="REPLAY SETTINGS";value=t3_practice_replay_name(m->replay_setting);break;
        case T3_PM_ROW_KEY_DISPLAY:label="KEY DISPLAY";value=OFF_ON[m->key_display&1u];break;
        case T3_PM_ROW_COMBO_PLAYER: {
            /* `COMBO PLAYER n-` and the character's name at x 207 */
            const unsigned who=m->combo_player&1u;
            char text[24]="COMBO PLAYER ";
            unsigned n=13;
            text[n++]=(char)('1'+who);text[n++]='-';text[n]=0;
            t3_practice_font_text(0,colour,text,72,y);
            if(view->player_name[who])t3_practice_font_text(0,colour,view->player_name[who],207,y);
            y+=22;
            continue;
        }
        case T3_PM_ROW_COMBO_TYPE:
            label="COMBO TYPE";
            number[0]=(char)('1'+m->combo_type%8u);
            value=m->combo_count[m->combo_player&1u]?number:"NONE";
            break;
        case T3_PM_ROW_RETURN:label="RETURN TO MODE SELECT";y=328;break;
        default:break;
        }
        if(row==T3_PM_ROW_DUMMY) {   /* the label, then the setting on the next line */
            t3_practice_font_text(0,colour,label,72,y);
            y+=22;
            value_text(colour,value,y);
        } else {
            t3_practice_font_text(0,colour,label,72,y);
            if(value[0])value_text(colour,value,y);
        }
        y+=22;
        if(row==T3_PM_ROW_COMMAND_LIST)y=162;
    }
    return 1;
}
