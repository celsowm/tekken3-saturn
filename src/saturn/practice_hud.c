/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from the pinned Tekken3Remake (practice_sim.py, practice_view.gd); see practice_hud.h and NOTICE.md. */
#include "practice_hud.h"
#include "practice_font.h"
#include "practice_menu.h"
#include "practice_icons.h"
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
    const char* prompt=0;unsigned colour=1,arrow=0;
    if(hud->sub==T3_PM_SUB_COMBO || (hud->sub==T3_PM_SUB_FREE && hud->recorder)) {
        if(hud->sub==T3_PM_SUB_COMBO && !hud->combo_known){prompt=0;}
        else switch(st->guide.state) {
            case T3_GUIDE_READY:
                if(hud->sub==T3_PM_SUB_COMBO){prompt="PLAY = SELECT";colour=1;}
                else {prompt="PLAY(SEL) REC( +SEL)";colour=7;arrow=1;}   /* the USA's QUICK ROLL table: a down arrow in the gap */
                break;
            case T3_GUIDE_RECORD_WAIT:prompt="START REC = ANY KEY";colour=1;break;
            case T3_GUIDE_RECORDING:prompt="REC (STOP = SEL)";colour=2;break;
            case T3_GUIDE_PLAY:prompt="PLAY";colour=5;break;
            case T3_GUIDE_MODE:prompt="GUIDE MODE";colour=6;break;
            default:break;
        }
    } else if(hud->replay_setting==T3_REPLAY_MANUAL && hud->sub==T3_PM_SUB_FREE){prompt="REPLAY = SELECT";colour=1;}
    if(prompt)t3_practice_font_text(0,colour,prompt,(int)(1u*adv),(int)(21u*line));
    if(prompt && arrow)(void)t3_practice_icons_tech_arrow(1u,colour,(int)(1u*adv)+14*(int)adv,0x17B);   /* the 'd' of PLAY(SEL) REC(d+SEL) */
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

/* ---- the command list screen (FUN_800789E4): cards 64 pixels apart in a 182-pixel column, the cursor's move on top ---- */
static unsigned lerp5(unsigned a,unsigned b,unsigned step,unsigned steps) {return a+(b>a?(b-a)*step/steps:0u)-(a>b?(a-b)*step/steps:0u);}


/* A command: ASCII at 6 pixels a cell, the arrows (0x80-0x90) and buttons (0x91-0xA0) as 20-pixel pictures. */
static void command_text(const char* command,int x,int y) {
    static const uint16_t DIRECTION[8]={0xC000u,0x4000u,0x6000u,0x8000u,0x2000u,0x9000u,0x1000u,0x3000u};   /* db d df b f ub u uf */
    char run[40];
    unsigned n=0;
    for(const uint8_t* c=(const uint8_t*)command;;++c) {
        if(n && (*c==0 || *c>=0x80 || n>=sizeof run-1u)) {
            run[n]=0;
            (void)t3_practice_icons_glyphs(run,x,y);
            x+=(int)n*6;n=0;
        }
        if(!*c)break;
        if(*c<0x80){run[n++]=(char)*c;continue;}
        if(*c<=0x90) {
            const unsigned index=*c-0x80u;
            (void)t3_practice_icons_arrow(index==16u?0u:DIRECTION[index&7u],index&8u?1u:0u,x,y-12);
        } else if(*c<=0xA0) {
            const unsigned mask=*c-0x91u;
            const unsigned pad=((mask&1u)?0x80u:0u)|((mask&2u)?0x10u:0u)|((mask&4u)?0x40u:0u)|((mask&8u)?0x20u:0u);
            (void)t3_practice_icons_button(pad,0u,x,y-12);
        }
        x+=20;
    }
}

/* A rectangle cut to the list's window (y 116 to 436: the original's draw areas). */
static void window_panel(int x,int y,int w,int h,unsigned rgb555) {
    const int top=116,bottom=436;
    if(y<top){h-=top-y;y=top;}
    if(y+h>bottom)h=bottom-y;
    if(h>0)panel(x,y,w,h,rgb555);
}
static int in_window(int y,int h) {return y>=116 && y+h<=436;}

/* The special cards: character 6's lines 6-13 and 14 on are green and gold, the first of each group a flat header. */
static void card_colours(unsigned character,unsigned index,unsigned* edge,unsigned* fill,int* boxed) {
    *edge=SAT_RGB555(2,4,12);*fill=SAT_RGB555(8,18,23);*boxed=1;
    if(character!=6u)return;
    int first=0;
    if(index>=6u && index<=13u){*edge=SAT_RGB555(0,8,4);*fill=SAT_RGB555(10,20,11);first=index<7u;}
    else if(index>=14u){*edge=SAT_RGB555(8,6,2);*fill=SAT_RGB555(20,18,4);first=index<15u;}
    if(first){*fill=SAT_RGB555(2,4,12);*boxed=0;}
}

static void command_column(const t3_practice_menu_view_t* view,unsigned column,int x0) {
    const t3_practice_command_column_t* col=&view->column[column];
    if(!col->active || !col->lines)return;
    /* FUN_800789E4: the offset slides the cards; the first one drawn starts above the window */
    const int off=col->offset;
    const int shift=off/64+(off>0?1:0);
    int y=off+118-64*shift;
    int index=(int)col->cursor-shift;
    while(index<0)index+=(int)col->lines;
    while(index>=(int)col->lines)index-=(int)col->lines;
    unsigned rows=6;
    if(col->character==15u){y=246;rows=1;}   /* a single card, mid screen */
    for(unsigned row=0;row<rows;++row,y+=64) {
        const char *name,*command;
        if(!view->command_line(view->context,column,(unsigned)index,&name,&command))break;
        unsigned edge,fill;
        int boxed;
        card_colours(col->character,(unsigned)index,&edge,&fill,&boxed);
        window_panel(x0+2,y-3,180,66,SAT_RGB555(26,26,26));
        window_panel(x0+3,y-2,178,63,edge);
        if(boxed) {
            for(unsigned band=0;band<6u;++band)
                window_panel(x0+3,y-2+(int)band*4,178,4,SAT_RGB555(lerp5((edge>>0)&31u,(fill>>0)&31u,band,5),
                    lerp5((edge>>5)&31u,(fill>>5)&31u,band,5),lerp5((edge>>10)&31u,(fill>>10)&31u,band,5)));
        } else window_panel(x0+3,y-2,178,26,fill);
        unsigned length=0;
        while(name[length])++length;
        const int name_y=length>=29u?y:y+6;
        if(in_window(name_y,16)) {
            char first[29];
            unsigned i;
            for(i=0;name[i] && i<28u;++i)first[i]=name[i];
            first[i]=0;
            (void)t3_practice_icons_glyphs(first,x0+6,name_y);
        }
        if(length>=29u && in_window(y+12,16)) {   /* a long name wraps at 28 cells */
            char rest[40];
            unsigned i;
            for(i=0;name[28+i] && i<39u;++i)rest[i]=name[28+i];
            rest[i]=0;
            (void)t3_practice_icons_glyphs(rest,x0+6,y+12);
        }
        if(in_window(y+27,32))command_text(command,x0+6,y+39);
        if(++index>=(int)col->lines)index=0;
    }
}

static int command_screen(const t3_practice_menu_view_t* view) {
    /* the cards first, then the title bars and rules over the window's edges */
    command_column(view,0,0);
    command_column(view,1,184);
    for(unsigned column=0;column<2u;++column) {
        if(!view->column[column].active)continue;
        const int x0=column?184:0;
        panel(x0+1,92,182,23,SAT_RGB555(2,4,12));
        panel(x0+2,114,180,2,SAT_RGB555(28,28,28));
        panel(x0+2,435,180,2,SAT_RGB555(28,28,28));
        /* the USA list's title is the atlas word 0x01 (STR_TITLE at 0x80027C4C: "\xFFp\x01"), "When Facing Right", at (x0 + 0x27, 0x61) */
        (void)t3_practice_icons_glyphs("When Facing Right",x0+39,97);
    }
    /* "%cPUSH %cBUTTON %cTO %cEXIT" in colours 6, 5, 6, 5 (practice_command_list) */
    t3_practice_font_text(0,6,"PUSH",6,440);t3_practice_font_text(0,5,"BUTTON",6+5*9,440);
    t3_practice_font_text(0,6,"TO",6+12*9,440);t3_practice_font_text(0,5,"EXIT",6+15*9,440);
    return 1;
}

int t3_practice_menu_draw(const t3_practice_menu_view_t* view) {
    if(!t3_practice_font_ready())return 0;
    const t3_practice_menu_t* m=view->menu;
    if(m->command_open)return command_screen(view);
    backdrop(m->mode_select);
    if(m->mode_select) {
        /* FUN_800B5834 */
        static const char* const LABELS[T3_PM_SELECT_ROWS]={"1P FREESTYLE","VS CPU","COMBO TRAINING","PLAYER SELECT","RESET"};
        t3_practice_font_text(1,10,"MODE SELECT",16,56);
        for(unsigned r=0;r<T3_PM_SELECT_ROWS;++r)
            t3_practice_font_text(0,r==m->select_cursor?10u:11u,LABELS[r],16,110+(int)r*22);
        return 1;
    }
    /* FUN_800B5978, FUN_800B5ED4, FUN_800B6368: the page; row `r` of the item list keeps the colour of the cursor. */
    static const char* const DIFFICULTY[3]={"EASY","MEDIUM","HARD"};
    static const char* const OFF_ON[2]={"OFF","ON"};
    static const char* const TITLES[T3_PM_SUBS]={"1P FREESTYLE","VS.CPU","COMBO TRAINING"};   /* the USA wording */
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
        case T3_PM_ROW_FREEZE_SIGNAL:label="HIT ANALYSIS";value=OFF_ON[m->freeze_signal&1u];break;
        case T3_PM_ROW_REPLAY:label="COMBO REPLAY";value=t3_practice_replay_name(m->replay_setting);break;
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
            if(m->dummy==T3_DUMMY_UKEMI_MAE || m->dummy==T3_DUMMY_UKEMI_OKU) {
                /* FUN_800B7704, the USA's QUICK ROLL: the words and an arrow (down for the front roll, up for the back) at x 198 */
                t3_practice_font_text(0,colour,"QUICK ROLL",72+126,y);
                (void)t3_practice_icons_tech_arrow(m->dummy==T3_DUMMY_UKEMI_MAE,colour,(72+126+11*9),y);
            } else value_text(colour,value,y);
        } else {
            t3_practice_font_text(0,colour,label,72,y);
            if(value[0])value_text(colour,value,y);
        }
        y+=22;
        if(row==T3_PM_ROW_COMMAND_LIST)y=162;
    }
    return 1;
}
