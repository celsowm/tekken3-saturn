/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of force.ovl FUN_800B1778 as ported in the pinned Tekken3Remake
 * tools/research/force_sim.py (level_script, _commands, _bonus_frame, tally, _boss, _patterns,
 * _enemy_slot, level_frame's caps). Changes: see psx_force_level.h. */
#include "psx_force_level.h"

/* Offsets in the tables (original 0x800B0A10 + offset). */
enum {
    PATTERNS=0,           /* 6-byte records: costume, type, 0; 99 ends a pattern */
    KILL_SECONDS=0x128,   /* 0x800B0B38: per type u16 ?, u16 seconds */
    ENEMY_HEALTH=0x13C,   /* 0x800B0B4C: per level 5 u32 (16.16) */
    ENEMY_KEYS=0x1A0,     /* 0x800B0BB0: per level two costume keys */
    ALLOWANCE=0x1AC,      /* 0x800B0BBC: per level bonus time allowance (frames) */
    STAGE_BONUS=0x1C0     /* 0x800B0BD0: per level */
};
enum {SCORE_CAP=99999990,TIMER_CAP=0x1734,CLEAR_PATTERN=0x63};

static uint16_t tab16(const uint8_t* t,int64_t off){
    if(off<0||off+2>(int64_t)T3_FORCE_TABLE_BYTES)return 0;
    return (uint16_t)(t[off]|(t[off+1]<<8));
}
static uint32_t tab32(const uint8_t* t,int64_t off){
    return (uint32_t)tab16(t,off)|((uint32_t)tab16(t,off+2)<<16);
}
int t3_force_tables_valid(const uint8_t* t,size_t bytes){
    if(!t || bytes!=T3_FORCE_TABLE_BYTES)return 0;
    for(unsigned i=0;i<25;++i)if((int32_t)tab32(t,ENEMY_HEALTH+4u*i)<=0)return 0;
    for(unsigned i=0;i<8;++i)if(t[ENEMY_KEYS+i]>3u)return 0;
    for(unsigned i=0;i<4;++i)if(!tab32(t,STAGE_BONUS+4u*i))return 0;
    return 1;
}

static uint8_t tab8(const uint8_t* t,int64_t off){
    return off<0||off>=(int64_t)T3_FORCE_TABLE_BYTES?0:t[off];
}
static int16_t rec16(const uint8_t* r,int off){return (int16_t)(r[off]|(r[off+1]<<8));}
static int32_t rec32(const uint8_t* r,int off){
    return (int32_t)((uint32_t)r[off]|((uint32_t)r[off+1]<<8)|((uint32_t)r[off+2]<<16)|((uint32_t)r[off+3]<<24));
}
static const uint8_t* record(const uint8_t* script,int32_t index){
    static const uint8_t zero[20]={0};
    return index<0||index>=T3_FORCE_SCRIPT_RECORDS?zero:script+20*index;
}
static void emit(t3_force_level_t* s,int32_t kind,int32_t a,int32_t b,int32_t c){
    if(s->event_count<T3_FORCE_EVENTS){
        t3_force_event_t* e=&s->events[s->event_count++];
        e->kind=kind;e->a=a;e->b=b;e->c=c;
    }
}
static int32_t wrap32(int64_t v){return (int32_t)(uint32_t)(uint64_t)v;}

static int free_slot(const t3_force_level_t* s){
    for(int i=0;i<T3_FORCE_SLOTS;++i)if(s->slot[i].state==1)return i;
    return -1;
}
static void slot_release(t3_force_level_t* s,int i){
    t3_force_fighter_t* f=&s->fighter[i];
    f->health=0;f->fc3=0;f->fc2=1;s->slot[i].state=1;
}
static void scroll_mode(t3_force_level_t* s,int32_t mode){
    /* the original's free-scroll flag (0x800B70C4) is also bound[1] of the camera record */
    if(mode==0)s->bound[1]=1;
    else if(mode==2){
        s->bound[0]=-100;s->bound[1]=60;
        s->bound[2]=wrap32((int64_t)s->lock_x-5000);s->bound[3]=wrap32((int64_t)s->lock_x+5000);
    }
    s->free_scroll=s->bound[1];
    s->scroll_mode=mode;
}
static void spawn(t3_force_level_t* s,int i,uint16_t costume,uint16_t kind,int32_t x,int32_t z){
    t3_force_slot_t* sl=&s->slot[i];
    t3_force_fighter_t* f=&s->fighter[i];
    if((int32_t)sl->loaded==(int32_t)costume)sl->state=4;
    else{sl->state=2;sl->requested=costume;}
    sl->type=(int16_t)((int16_t)kind%5);
    f->anchor_x=x;f->root_x=x;f->root_z=z;f->anchor_z=z;
}

/* FUN_800B4328: the first free item slot gets the item. */
static void place_item(t3_force_level_t* s,int32_t x,int32_t z){
    for(int i=0;i<T3_FORCE_ITEMS;++i){
        t3_force_item_t* it=&s->item[i];
        if(it->state==0){it->x=x;it->z=z;it->state=1;return;}
    }
}

static void commands(t3_force_level_t* s,const uint8_t* t,const uint8_t* script,int32_t progress){
    (void)t;
    if(s->round_state!=1||progress<rec32(record(script,s->script_ptr),0))return;
    for(int guard=0;guard<T3_FORCE_SCRIPT_RECORDS;++guard){
        const uint8_t* r=record(script,s->script_ptr);
        int stop=0;
        const int16_t cmd=rec16(r,4);
        const int32_t cam=s->camera_x;
        if(cmd==0){
            s->scroll_param=rec16(r,0xE);
            scroll_mode(s,rec16(r,0xE));
        }else if(cmd==1){
            const int i=free_slot(s);
            if(i>=0)spawn(s,i,(uint16_t)rec16(r,0xE),(uint16_t)rec16(r,0x10),wrap32((int64_t)cam+rec16(r,6)),rec16(r,0xA));
        }else if(cmd==2){
            s->pattern_on=wrap32((int64_t)s->pattern_on+1);
            s->pattern_stop_x=(uint16_t)rec16(r,0x10);
            if((uint16_t)rec16(r,0xE)<0x31)s->pattern_ptr=6*rec16(r,0xE);
        }else if(cmd==3){
            stop=1;
            s->wait_defeated=0;s->state=1;s->wait_count=rec16(r,0xE);
        }else if(cmd==4){
            place_item(s,wrap32((int64_t)cam+rec16(r,6)),rec16(r,0xA));
        }else if(cmd==6){
            for(int i=0;i<T3_FORCE_SLOTS;++i)slot_release(s,i);
            emit(s,T3_FORCE_EVENT_MUSIC_FADE,0,0,0);
            stop=1;
            s->sub_mode=0;s->pattern_on=0;s->state=3;s->tally=wrap32((int64_t)s->clock+0x3C);
        }
        ++s->script_ptr;
        if(stop||progress<rec32(record(script,s->script_ptr),0))break;
    }
}

static void tally(t3_force_level_t* s,const uint8_t* t){
    const int32_t n=wrap32((int64_t)s->tally+1);
    s->tally=n;
    if(n>=0x1E){
        const uint32_t bonus=tab32(t,STAGE_BONUS+4*(int64_t)s->level);
        if(n==0x1E){s->score+=bonus;emit(s,T3_FORCE_EVENT_SOUND,0x4C6C,0,0);}
    }
    if(n>=0x3C&&n==0x3C){s->score+=(uint32_t)s->clear_bonus;emit(s,T3_FORCE_EVENT_SOUND,0x4C6C,0,0);}
    if(n>=0x5A&&n==0x5A)emit(s,T3_FORCE_EVENT_SOUND,0x4C6C,0,0);
    if(n<0x78)return;
    s->tally_done=1;
    const int32_t b=s->bonus_time;
    if(s->level<3&&b){
        if(n==0x78){s->timer=wrap32((int64_t)s->timer+b);emit(s,T3_FORCE_EVENT_SOUND,0x4C6C,0,0);}
    }else if(s->key_earned){
        if(n==0x78){++s->keys;emit(s,T3_FORCE_EVENT_SOUND,0x4CEB,0,0);}
    }else if(s->final_challenge){
        if(n==0x78)emit(s,T3_FORCE_EVENT_SOUND,0x4CEB,0,0);
    }
}

static void bonus_frame(t3_force_level_t* s,const uint8_t* t){
    const int64_t clear=(int64_t)s->fight_start_timer-s->timer;
    const int64_t bonus=(int64_t)(int32_t)tab32(t,ALLOWANCE+4*(int64_t)s->level)-clear;
    s->clear_time=clear>=0?wrap32(clear):0;
    s->bonus_time=bonus>=0?wrap32(bonus):0;
    if(s->round_state!=8)return;
    const int32_t health=s->player_health;
    s->clear_bonus=wrap32((int64_t)((int32_t)((int64_t)(health<0?(int64_t)health+0xFFFF:health)>>16))*100);
    if(s->level==3){
        if(s->keys<3&&!s->dr_b){
            s->key_earned=1;
            s->key_count=(uint8_t)(s->key_count+1>0xFF?0xFF:s->key_count+1);
            emit(s,T3_FORCE_EVENT_SAVE_PENDING,0,0,0);
            static const uint8_t flag[3]={1,3,7};
            if(s->keys>=0)s->mode_flags|=flag[s->keys];
        }else if(s->keys==3&&!s->dr_b)s->final_challenge=1;
    }
    s->tally=0;s->state=7;
}

static void boss(t3_force_level_t* s,const uint8_t* t){
    t3_force_slot_t* sl=&s->slot[0];
    t3_force_fighter_t* f=&s->fighter[0];
    sl->type=4;sl->state=1;
    emit(s,T3_FORCE_EVENT_START_MOVE,0,3,0);
    f->fc3=1;
    const int32_t hp=(int32_t)tab32(t,ENEMY_HEALTH+20*(int64_t)s->level+4*(int64_t)sl->type);
    f->fc2=0;f->f04=0;f->f92=0;f->fb8=0;
    f->health_max=hp;f->health=hp;
    emit(s,T3_FORCE_EVENT_ENEMY_ACTION,1,0x13,0);
    s->scroll_param=2;
    scroll_mode(s,2);
    s->flag_70e8=1;
    s->state=9;
    s->fight_start_timer=s->timer;
}

static void patterns(t3_force_level_t* s,const uint8_t* t,int32_t progress){
    if(s->pattern_on!=1)return;
    if(!(progress<s->pattern_stop_x)){s->pattern_on=0;return;}
    const int32_t at=s->pattern_ptr;
    if(tab16(t,at)==CLEAR_PATTERN)return;
    const int i=free_slot(s);
    if(i<0)return;
    spawn(s,i,tab16(t,at),tab16(t,at+2),wrap32((int64_t)s->camera_x+5000),0);
    s->pattern_ptr=at+6;
}

static int level_script(t3_force_level_t* s,const uint8_t* t,const uint8_t* script){
    int area_change=0;
    const int32_t progress=wrap32((int64_t)s->camera_x-s->origin);
    if(s->frozen==0)++s->clock;
    if(s->text_off==0)++s->drawn;
    const int32_t clock=s->clock;
    switch(s->state){
    case 0:commands(s,t,script,progress);break;
    case 1:
        if(s->wait_count<0){if(s->slot[0].state==1&&s->slot[1].state==1)s->state=0;}
        else if(!(s->wait_defeated<s->wait_count))s->state=0;
        break;
    case 3:
        if(s->tally<clock&&!s->frozen){s->tally=wrap32((int64_t)clock+2);s->state=4;}
        break;
    case 4:
        if(s->tally<clock&&!s->frozen){area_change=1;s->state=5;}
        break;
    case 5:
        s->flag_70e8=1;s->sub_mode=1;s->state=6;s->fight_start_timer=s->timer;
        break;
    case 6:bonus_frame(s,t);break;
    case 7:tally(s,t);break;
    case 8:boss(s,t);break;
    case 9:{
        const int64_t d=(int64_t)s->fight_start_timer-s->timer;
        s->clear_time=wrap32(d>0?d:0);
        if(s->round_state==8){s->tally=0;s->state=10;s->mode_flags|=8;}
        break;}
    case 10:{
        const int32_t n=s->tally;
        s->tally=wrap32((int64_t)n+1);
        if(n>=0xB5)s->tally_done=1;
        break;}
    default:break;
    }
    patterns(s,t,progress);
    return area_change;
}

static void enemy_slot(t3_force_level_t* s,const uint8_t* t,int i){
    t3_force_slot_t* sl=&s->slot[i];
    t3_force_fighter_t* f=&s->fighter[i];
    const int32_t clock=s->clock;
    switch(sl->state){
    case 0:f->fc3=0;f->health=0;f->fc2=1;sl->state=1;break;
    case 2:f->fc3=0;sl->state=3;sl->timer=(uint16_t)(s->drawn+2);break;
    case 3:
        if((int32_t)(int16_t)sl->timer<s->drawn&&!s->text_off){
            const uint16_t costume=sl->requested;
            sl->loaded=(int16_t)costume;
            const uint8_t key=tab8(t,ENEMY_KEYS+2*(int64_t)(s->level&3)+((int16_t)costume&1));
            f->key=(uint16_t)(key+0x54);
            emit(s,T3_FORCE_EVENT_MODEL_RESET,i,0,0);
            emit(s,T3_FORCE_EVENT_MODEL_REQUEST,f->index,sl->loaded+1,0);
            emit(s,T3_FORCE_EVENT_MODEL_BIND,i,0,0);
            emit(s,T3_FORCE_EVENT_ENEMY_ACTION,f->index,0x15,key);
            sl->state=4;
        }
        break;
    case 4:{
        f->fc3=1;
        const int32_t hp=(int32_t)tab32(t,ENEMY_HEALTH+20*(int64_t)s->level+4*(int64_t)sl->type);
        f->fc2=0;f->health_max=hp;f->health=hp;
        sl->state=5;
        emit(s,T3_FORCE_EVENT_START_MOVE,i,3,0);
        f->f04=0;f->f92=0;f->fb8=0;
        break;}
    case 5:
        if(f->health>0)return;
        sl->state=6;
        sl->timer=(uint16_t)(clock+0x3C);
        if(f->owner!=s->player_index&&f->fe7==0)return;
        {
            const int32_t tm=s->timer;
            const uint16_t sec=tab16(t,KILL_SECONDS+4*(int64_t)sl->type+2);
            s->timer=wrap32((int64_t)tm+59-tm%60+(int64_t)sec*60);
            sl->popup_x=(uint16_t)(((uint32_t)f->screen_x&0xFFFFu)-0x1Bu);
            sl->popup_y=(uint16_t)((uint32_t)f->screen_y<<4);
            sl->popup_until=(uint16_t)(clock+0x3C);
            s->kills=(uint16_t)(s->kills+1);
        }
        break;
    case 6:
        if(clock<(int16_t)sl->popup_until&&s->frozen==0)sl->popup_y=(uint16_t)(sl->popup_y-8);
        if((int32_t)(int16_t)sl->timer<clock&&f->fc4==0){
            sl->state=7;sl->timer=(uint16_t)(clock+0x28);f->fc2=1;
        }else if(f->fce&&f->owner==s->player_index)sl->timer=(uint16_t)(clock+0x3C);
        break;
    case 7:
        f->fc3=(uint8_t)(clock&1);
        if((int32_t)(int16_t)sl->timer<clock){
            ++s->wait_defeated;f->fc3=0;sl->state=1;
        }
        break;
    default:break;
    }
}

/* FUN_800B4374: a placed item is removed 5,000 units behind the camera; the player picks it up within 500
 * units in x and z (health +50, sound 0x4E6A), which starts LIFE UP! (60 frames, sound 0x87C0 15 frames
 * before the end). Drawing is the caller's. */
static void items(t3_force_level_t* s){
    for(int i=0;i<T3_FORCE_ITEMS;++i){
        t3_force_item_t* it=&s->item[i];
        if(it->state==1){
            if(it->x<wrap32((int64_t)s->camera_x-5000)){it->state=0;continue;}
            const int64_t dx=(int64_t)wrap32((int64_t)s->player.x-it->x),dz=(int64_t)wrap32((int64_t)s->player.z-it->z);
            if((dx<0?-dx:dx)<0x1F5&&(dz<0?-dz:dz)<0x1F5&&s->player_health>0&&s->player.db==0){
                s->player_health=wrap32((int64_t)s->player_health+0x320000);
                emit(s,T3_FORCE_EVENT_SOUND,0x4E6A,0,0);
                it->state=2;
                it->until=wrap32((int64_t)s->clock+0x3C);
                it->popup_x=(uint16_t)(((uint32_t)s->player.screen_x&0xFFFFu)-0x24u);
                it->popup_y=(uint16_t)((uint32_t)s->player.screen_z<<4);
                emit(s,T3_FORCE_EVENT_HEALTH_FLASH,0,0,0);
            }
        }else if(it->state==2){
            if(s->frozen==0)it->popup_y=(uint16_t)(it->popup_y-8);
            if((uint32_t)s->clock==(uint32_t)it->until-0xFu)emit(s,T3_FORCE_EVENT_SOUND,0x87C0,0,0);
            if(it->until<s->clock)it->state=0;
        }
    }
}

void t3_force_level_step(t3_force_level_t* s,const uint8_t* t,const uint8_t* script){
    s->event_count=0;
    if(level_script(s,t,script))emit(s,T3_FORCE_EVENT_AREA_CHANGE,0,0,0);
    for(int i=0;i<T3_FORCE_SLOTS;++i)enemy_slot(s,t,i);
    s->bar_phase+=0x40; /* every frame the level runs (the pause menu stops the fight) */
    if(!s->bar_done&&s->flag_70e8)s->bar_done=1;
    items(s);
    if(s->score>(uint32_t)SCORE_CAP)s->score=(uint32_t)SCORE_CAP;
    if(s->hiscore_run<s->score){s->hiscore_flag=1;s->hiscore_run=s->score;}
    if(s->timer>=TIMER_CAP+1)s->timer=TIMER_CAP;
    if(s->frozen==0&&s->round_state==1){
        const int32_t tm=s->timer;
        if(tm==60||tm==120||tm==180||tm==240||tm==300)emit(s,T3_FORCE_EVENT_SOUND,s->countdown_sound[tm/60-1],0,0);
    }
}

#define LOAD(field) (s->field=v[n++])
void t3_force_level_load(t3_force_level_t* s,const int32_t* v){
    unsigned n=0;
    LOAD(level);LOAD(state);LOAD(clock);LOAD(drawn);LOAD(tally);LOAD(tally_done);
    s->score=(uint32_t)v[n++];s->hiscore_run=(uint32_t)v[n++];
    LOAD(timer);LOAD(fight_start_timer);LOAD(keys);LOAD(key_earned);LOAD(final_challenge);LOAD(wait_defeated);LOAD(wait_count);
    LOAD(pattern_on);LOAD(pattern_stop_x);LOAD(pattern_ptr);LOAD(script_ptr);
    LOAD(scroll_mode);LOAD(scroll_param);LOAD(clear_time);LOAD(bonus_time);LOAD(clear_bonus);
    LOAD(frozen);LOAD(text_off);LOAD(round_state);LOAD(camera_x);LOAD(origin);LOAD(player_health);LOAD(dr_b);LOAD(player_index);LOAD(lock_x);
    s->mode_flags=(uint8_t)v[n++];s->sub_mode=(uint8_t)v[n++];LOAD(flag_70e8);
    s->key_count=(uint8_t)v[n++];s->kills=(uint16_t)v[n++];s->hiscore_flag=(uint8_t)v[n++];
    for(int i=0;i<4;++i)s->bound[i]=v[n++];
    s->free_scroll=s->bound[1];++n;
    for(int i=0;i<T3_FORCE_SLOTS;++i){
        t3_force_slot_t* sl=&s->slot[i];
        sl->state=(int16_t)v[n++];sl->timer=(uint16_t)v[n++];sl->type=(int16_t)v[n++];sl->loaded=(int16_t)v[n++];
        sl->requested=(uint16_t)v[n++];sl->popup_x=(uint16_t)v[n++];sl->popup_y=(uint16_t)v[n++];sl->popup_until=(uint16_t)v[n++];
    }
    for(int i=0;i<T3_FORCE_SLOTS;++i){
        t3_force_fighter_t* f=&s->fighter[i];
        f->health=v[n++];f->health_max=v[n++];f->f04=v[n++];f->f92=(uint16_t)v[n++];
        f->fb8=(uint8_t)v[n++];f->fc2=(uint8_t)v[n++];f->fc3=(uint8_t)v[n++];f->fc4=(uint8_t)v[n++];
        f->fce=(uint8_t)v[n++];f->fe7=(uint8_t)v[n++];f->owner=(uint8_t)v[n++];f->index=(uint8_t)v[n++];
        f->key=(uint16_t)v[n++];
        f->anchor_x=v[n++];f->anchor_z=v[n++];f->root_x=v[n++];f->root_z=v[n++];f->screen_x=v[n++];f->screen_y=v[n++];
    }
    for(int i=0;i<T3_FORCE_ITEMS;++i){
        t3_force_item_t* it=&s->item[i];
        it->state=(uint32_t)v[n++];it->x=v[n++];it->z=v[n++];it->popup_x=(uint16_t)v[n++];it->popup_y=(uint16_t)v[n++];it->until=v[n++];
    }
    s->player.x=v[n++];s->player.z=v[n++];s->player.db=(uint8_t)v[n++];s->player.screen_x=v[n++];s->player.screen_z=v[n++];
}
#undef LOAD

#define SAVE(field) (v[n++]=s->field)
void t3_force_level_save(const t3_force_level_t* s,int32_t* v){
    unsigned n=0;
    SAVE(level);SAVE(state);SAVE(clock);SAVE(drawn);SAVE(tally);SAVE(tally_done);
    v[n++]=(int32_t)s->score;v[n++]=(int32_t)s->hiscore_run;
    SAVE(timer);SAVE(fight_start_timer);SAVE(keys);SAVE(key_earned);SAVE(final_challenge);SAVE(wait_defeated);SAVE(wait_count);
    SAVE(pattern_on);SAVE(pattern_stop_x);SAVE(pattern_ptr);SAVE(script_ptr);
    SAVE(scroll_mode);SAVE(scroll_param);SAVE(clear_time);SAVE(bonus_time);SAVE(clear_bonus);
    SAVE(frozen);SAVE(text_off);SAVE(round_state);SAVE(camera_x);SAVE(origin);SAVE(player_health);SAVE(dr_b);SAVE(player_index);SAVE(lock_x);
    v[n++]=s->mode_flags;v[n++]=s->sub_mode;SAVE(flag_70e8);
    v[n++]=s->key_count;v[n++]=s->kills;v[n++]=s->hiscore_flag;
    for(int i=0;i<4;++i)v[n++]=s->bound[i];
    SAVE(free_scroll);
    for(int i=0;i<T3_FORCE_SLOTS;++i){
        const t3_force_slot_t* sl=&s->slot[i];
        v[n++]=sl->state;v[n++]=sl->timer;v[n++]=sl->type;v[n++]=sl->loaded;
        v[n++]=sl->requested;v[n++]=sl->popup_x;v[n++]=sl->popup_y;v[n++]=sl->popup_until;
    }
    for(int i=0;i<T3_FORCE_SLOTS;++i){
        const t3_force_fighter_t* f=&s->fighter[i];
        v[n++]=f->health;v[n++]=f->health_max;v[n++]=f->f04;v[n++]=f->f92;
        v[n++]=f->fb8;v[n++]=f->fc2;v[n++]=f->fc3;v[n++]=f->fc4;
        v[n++]=f->fce;v[n++]=f->fe7;v[n++]=f->owner;v[n++]=f->index;
        v[n++]=f->key;
        v[n++]=f->anchor_x;v[n++]=f->anchor_z;v[n++]=f->root_x;v[n++]=f->root_z;v[n++]=f->screen_x;v[n++]=f->screen_y;
    }
    for(int i=0;i<T3_FORCE_ITEMS;++i){
        const t3_force_item_t* it=&s->item[i];
        v[n++]=(int32_t)it->state;v[n++]=it->x;v[n++]=it->z;v[n++]=it->popup_x;v[n++]=it->popup_y;v[n++]=it->until;
    }
    v[n++]=s->player.x;v[n++]=s->player.z;v[n++]=s->player.db;v[n++]=s->player.screen_x;v[n++]=s->player.screen_z;
}
#undef SAVE

void t3_force_level_setup(t3_force_level_t* s,int level){
    *s=(t3_force_level_t){0};
    s->level=level;
    /* The announcer's calls at 1..5 seconds left (the table at 0x8009747E of Rev.1, bank 2: the system VAB). */
    static const uint16_t countdown[5]={0x86D0,0x8711,0x8652,0x8693,0x8694};
    for(int i=0;i<5;++i)s->countdown_sound[i]=countdown[i];
    for(int i=0;i<T3_FORCE_SLOTS;++i){
        s->slot[i].loaded=(int16_t)i;
        s->fighter[i].fc2=1;
        s->fighter[i].index=(uint8_t)(i+1);
    }
    if(level>3)s->state=8;
}

uint32_t t3_force_stage_bonus(const uint8_t* t,int level){
    return t && level>=0 && level<4?tab32(t,STAGE_BONUS+4*(int64_t)level):0u;
}

int t3_force_bar_width(const uint8_t* t,int type){
    return t && type>=0 && type<=4?tab16(t,KILL_SECONDS+4*(int64_t)type):0;
}

int32_t t3_force_enemy_health(const t3_force_level_t* s,const uint8_t* t,int type){
    return (int32_t)tab32(t,ENEMY_HEALTH+20*(int64_t)s->level+4*(int64_t)type);
}
