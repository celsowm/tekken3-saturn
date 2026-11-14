/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from Tekken3Remake remake/core/game/practice_mode.gd; see practice_data.h and NOTICE.md. */
#include "practice_data.h"

void t3_practice_data_reset(t3_practice_data_t* d) {
    *d=(t3_practice_data_t){{{0}}};
}

/* move_system.gd _count_react_chain: consecutive reactions, reset outside them. */
int16_t t3_practice_react_chain(int16_t chain,const t3_practice_facts_t* f) {
    const int step=(f->move_flags&T3_PRACTICE_FLAG_REACT_CHAIN)!=0;
    const int run=f->hit_clean ||
        ((f->pose_flags&T3_PRACTICE_FLAG_REACT_CHAIN) && step) || (f->throw_state<0 && step);
    if(run)return (int16_t)(chain+1);
    if(!f->in_reaction && f->throw_state>=0)return 0;
    if(!f->step_transition && f->thrown_transition)return (int16_t)(chain+1);
    return chain;
}

/* _throw_flag: whether the opponent's move is a throw, and whether its previous one was. */
static void throw_flag(t3_practice_player_t* p,const t3_practice_facts_t* o) {
    if(!o->move_changed)return;
    p->throw_before=p->throw_now;
    uint8_t t=(o->move_flags&T3_PRACTICE_FLAG_BUFFER_INPUT)!=0;
    const unsigned slot=o->cur_slot;
    if(o->bank_type==T3_PRACTICE_BANK_YOSHIMITSU && (slot==0x12Du||slot==0x131u||slot==0x136u||slot==0x484u))t=1;
    if(o->bank_type==T3_PRACTICE_BANK_XIAOYU && slot>=0x5A1u && slot<0x5A1u+3u)t=1;
    if(o->bank_type==T3_PRACTICE_BANK_BRYAN && slot>=0x832u && slot<0x832u+3u)t=1;
    p->throw_now=t;
}

/* _count_combo: kept while the opponent reacts, counting hits and throw steps, up to 99. */
static void count_combo(t3_practice_player_t* p,const t3_practice_facts_t* f,const t3_practice_facts_t* o) {
    const int no_throw=!p->throw_now || !p->throw_before;
    if(!f->hit_clean && !f->last_extra_damage) {
        if(no_throw) {
            if(!f->was_hit_this_move && !f->react_chain)p->combo=0;
        } else if(o->whiffed)p->combo=0;
    } else if(f->throw_state<0) {
        if(!f->last_extra_damage)++p->combo;
        else if(p->hit_id!=(int8_t)o->throw_state) {
            if(!(o->bank_type==T3_PRACTICE_BANK_KING && o->cur_slot==0xB82u))++p->combo;
            p->hit_id=(int8_t)o->throw_state;
        }
    } else if(!f->react_chain && no_throw)p->combo=1;
    else ++p->combo;
    if((int8_t)p->combo>99){p->combo=0;p->marker=0x5A;}
    if((int8_t)p->combo>98)p->combo=99;
}

/* _combo_markers: a new best or an ended combo, and the marker's frames. */
static void combo_markers(t3_practice_player_t* p) {
    p->new_best=0;p->ended=0;
    const uint8_t combo=p->combo;
    if(combo!=p->previous) {
        if(p->previous<combo){p->new_best=combo;p->best=combo;}
        if(combo==0)p->ended=p->previous;
    }
    if(combo>=2 && p->new_best)p->marker=0x5A;
    if(combo<2 && p->hits>1)p->marker=0x5A;
    if(p->marker>0)--p->marker;
    p->previous=p->combo;
}

/* _damage: the hit's shown damage and share, the combo damage (capped at 999) and the total. */
static void damage(t3_practice_player_t* p,const t3_practice_facts_t* f) {
    int32_t add;
    if(!f->got_hit && !f->hit_clean && !f->last_extra_damage)add=f->damage;
    else {
        int16_t shown=f->last_extra_damage,share=shown;
        if(share==0) {
            shown=0;
            if(f->hit_clean){shown=f->base_damage;share=f->damage;}
        }
        p->hit_damage=shown;p->hit_share=share;
        if(p->combo<2 && p->hits<2)p->combo_damage=share;
        else {p->combo_damage+=share;p->shown_damage=p->combo_damage;}
        if(p->combo_damage>999){p->combo_damage=0;p->shown_damage=999;}
        if(p->combo_damage<-999){p->combo_damage=0;p->shown_damage=-999;}
        add=f->last_extra_damage;
        if(add<1)add=f->damage;
    }
    p->total+=add;
}

/* _player_data(f, o): the hits the player f takes. */
static void player_data(t3_practice_player_t* p,const t3_practice_facts_t* f,const t3_practice_facts_t* o) {
    if(f->last_extra_damage)++p->hits;
    if(o->move_changed)p->hits=0;
    if(o->throw_state<1)p->hit_id=0;
    throw_flag(p,o);
    count_combo(p,f,o);
    combo_markers(p);
    if(f->hit_clean){p->counter=f->counter_hit!=0;p->clean=f->close_hit!=0;}
    if(f->throw_state<0){p->counter=0;p->clean=0;}
    damage(p,f);
}

void t3_practice_data_step(t3_practice_data_t* d,const t3_practice_facts_t facts[2]) {
    player_data(&d->p[0],&facts[0],&facts[1]);
    player_data(&d->p[1],&facts[1],&facts[0]);
}

void t3_practice_markers_step(t3_practice_markers_t* m,uint16_t player_attack,int dummy_hit_clean) {
    for(unsigned i=0;i<T3_PRACTICE_MARKERS;++i)m->frames[i]=(int16_t)(m->frames[i]>0?m->frames[i]-1:0);
    if(dummy_hit_clean) {
        unsigned k=(unsigned)m->index+1u;
        if(k>3u)k=0;
        m->index=(uint8_t)k;
        m->frames[k]=T3_PRACTICE_MARKER_FRAMES;m->attack[k]=(int16_t)player_attack;
    }
}
