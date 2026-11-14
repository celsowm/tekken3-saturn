/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from the pinned Tekken3Remake (practice_sim.py, practice_view.gd); see practice_hud.h and NOTICE.md. */
#include "practice_hud.h"
#include "practice_font.h"
#include "practice_menu.h"

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
