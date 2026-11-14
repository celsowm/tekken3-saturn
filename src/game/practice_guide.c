/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from Tekken3Remake remake/core/game/practice_mode.gd; see practice_guide.h and NOTICE.md. */
#include "practice_guide.h"

static uint16_t rd16(const uint8_t* p){return (uint16_t)(p[0]|(p[1]<<8));}
static uint32_t rd32(const uint8_t* p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}

/* Walks the blob to a kind's list; returns the offset of its count, or 0. */
static uint32_t kind_offset(const uint8_t* blob,uint32_t size,unsigned kind) {
    if(!blob || size<6 || blob[0]!='T' || blob[1]!='3' || blob[2]!='C' || blob[3]!='B')return 0;
    const unsigned kinds=rd16(blob+4);
    if(kind>=kinds)return 0;
    uint32_t at=6;
    for(unsigned k=0;k<kind;++k) {
        if(at+2>size)return 0;
        const unsigned count=rd16(blob+at);
        at+=2;
        for(unsigned c=0;c<count;++c) {
            if(at+4>size)return 0;
            at+=4u+4u*rd16(blob+at)+2u*rd16(blob+at+2);
        }
    }
    return at+2<=size?at:0;
}

unsigned t3_practice_combo_count(const uint8_t* blob,uint32_t size,unsigned bank_type) {
    const uint32_t at=kind_offset(blob,size,bank_type);
    return at?rd16(blob+at):0;
}

int t3_practice_combo_get(const uint8_t* blob,uint32_t size,unsigned bank_type,unsigned combo,t3_practice_combo_t* out) {
    uint32_t at=kind_offset(blob,size,bank_type);
    if(!at)return 0;
    const unsigned count=rd16(blob+at);
    if(combo>=count)return 0;
    at+=2;
    for(unsigned c=0;c<combo;++c)at+=4u+4u*rd16(blob+at)+2u*rd16(blob+at+2);
    if(at+4>size)return 0;
    out->n_steps=rd16(blob+at);out->n_moves=rd16(blob+at+2);
    out->steps=blob+at+4;out->moves=blob+at+4+4u*out->n_steps;
    return at+4u+4u*out->n_steps+2u*out->n_moves<=size;
}

/* _replay_rewind: the replay word back to the ring's first entry, the move log emptied. */
static void replay_rewind(t3_practice_guide_t* g) {
    if(g->ring.head<0)g->replay_word&=0x80000000u;
    else g->replay_word=g->ring.word[0];
    g->replay_pos=0;g->move_count=0;
    for(unsigned i=0;i<T3_KEY_RING_WORDS;++i)g->marks[i]=0;
}

void t3_practice_guide_reset(t3_practice_guide_t* g) {
    t3_practice_keys_reset(&g->ring);
    replay_rewind(g);
}

unsigned t3_practice_guide_load(t3_practice_guide_t* g,const t3_practice_combo_t* combo) {
    if(!combo || !combo->n_steps){t3_practice_keys_reset(&g->ring);return 0;}
    g->ring.head=combo->n_steps-1;
    for(unsigned i=0;i<combo->n_steps && i<T3_KEY_RING_WORDS;++i)g->ring.word[i]=rd32(combo->steps+4u*i);
    return combo->n_steps;
}

void t3_practice_guide_load_moves(t3_practice_guide_t* g,const t3_practice_combo_t* combo) {
    if(!combo || !combo->n_moves){t3_practice_keys_reset(&g->ring);return;}
    g->move_count=combo->n_moves;
    for(unsigned i=0;i<combo->n_moves && i<T3_KEY_RING_WORDS;++i)g->marks[i]=rd16(combo->moves+2u*i);
}

void t3_practice_guide_stop(t3_practice_guide_t* g) {
    g->state=T3_GUIDE_IDLE;g->scripted=0;g->prompt_timer=0;g->prompt=0;
}

/* _recording_end: the last entry counts once, and the replay rewinds. */
static void recording_end(t3_practice_guide_t* g) {
    uint32_t* at=&g->ring.word[g->ring.head];
    *at=(*at&0x8000FFFFu)|0x10000u;
    replay_rewind(g);
}

/* _guide_play (FUN_800B83A4): the next scripted word; 0 once the ring is played. */
static int guide_play(t3_practice_guide_t* g) {
    for(;;) {
        const uint32_t word=g->replay_word;
        const uint32_t kept=word&0x8000FFFFu;
        const int32_t count=((int32_t)(word<<1)>>17)-1;
        g->replay_word=kept|(((uint32_t)count&0x7FFFu)<<16);
        if((((uint32_t)count<<17)&0x80000000u)==0) {
            uint32_t direction=(g->replay_word>>8)&0xFFu;
            if(kept&0x80000000u) {
                if(direction&8u)direction=(direction&0xFFF7u)|2u;
                else if(direction&2u)direction=(direction&0xFFFDu)|8u;
            }
            g->script=(uint16_t)((((g->replay_word&0xFFu)<<4)|(direction<<12))&0xFFFFu);
            g->scripted=1;
            return 1;
        }
        const int32_t pos=g->replay_pos+1;
        g->replay_pos=pos;
        if(g->ring.head<pos || pos>=T3_KEY_RING_WORDS) {
            g->replay_pos=0;
            if(g->ring.head<0)g->replay_word&=0x80000000u;
            else g->replay_word=g->ring.word[0];
            return 0;
        }
        g->replay_word=g->ring.word[pos];
    }
}

/* _guide_move_log (FUN_800B8510): the move the fighter starts at its active frame. */
static void guide_move_log(t3_practice_guide_t* g,const t3_practice_guide_in_t* in) {
    if(!in->active_first_frame)return;
    int32_t n=g->move_count;
    g->marks[n<T3_KEY_RING_WORDS?n:0]=in->cur_slot;
    ++n;
    g->move_count=n>=T3_KEY_RING_WORDS?0:n;
}

/* _standing_ok (FUN_800B8904): lying down, the fighter holds up. */
static int standing_ok(t3_practice_guide_t* g,const t3_practice_guide_in_t* in) {
    g->script=in->lying?0x1000u:0u;
    return !in->lying;
}

/* _ready_to_replay (FUN_800B899C): once the fighter stands in its stance (a turned one first turns back). */
static int ready_to_replay(t3_practice_guide_t* g,const t3_practice_guide_in_t* in) {
    if(in->turned){g->script=in->move_pending?0u:0x8000u;return 0;}
    if(in->cur_slot==3 || in->cur_slot==4 || in->cur_slot==5 || in->cur_slot==in->stance_slot){g->script=0;return 1;}
    return 0;
}

static void guide_ready(t3_practice_guide_t* g,const t3_practice_guide_in_t* in) {
    const uint8_t t=g->prompt_timer;
    g->prompt_timer=(uint8_t)(t-1u);
    if(t==0){g->prompt_timer=0;g->prompt=T3_GUIDE_PROMPT_HIT;}
    if(in->sub==2 && in->combo_known) {
        if(in->select_pressed) {
            g->script=0;g->scripted=1;
            g->prompt=T3_GUIDE_PROMPT_GUIDE;g->guide_prompt=2;g->state=T3_GUIDE_STAND;
            return;
        }
    }
    if(in->sub!=0 || !g->guide_on || (!in->select_pressed && !g->resumed_input))return;
    if(!in->held_down) {
        if(in->held_horizontal)g->resumed_input=1;
        g->script=0;replay_rewind(g);g->scripted=1;
        g->guide_prompt=2;g->prompt=T3_GUIDE_PROMPT_NONE;g->state=T3_GUIDE_REPLAY;
    } else {
        t3_practice_keys_reset(&g->ring);
        g->guide_prompt=2;g->prompt=T3_GUIDE_PROMPT_NONE;g->state=T3_GUIDE_RECORD_WAIT;
    }
}

void t3_practice_guide_step(t3_practice_guide_t* g,const t3_practice_guide_in_t* in) {
    g->guide_on=in->guide_on;
    /* Every frame starts with the fighters on their pads again (_dummy_control sets input mode 1); only a frame that
     * scripts a word (guide_play, or Select starting the guide) takes the pad over. */
    g->scripted=0;
    switch(g->state) {
    case T3_GUIDE_IDLE:
        if(in->sub==2) {
            g->guide_prompt=1;
            replay_rewind(g);
            t3_practice_guide_load_moves(g,in->combo);
            g->guide_length=(uint8_t)t3_practice_guide_load(g,in->combo);
            if(!g->guide_length)g->guide_length=0xF;
            g->state=T3_GUIDE_READY;
        } else if(in->sub==0 && in->guide_on)g->state=T3_GUIDE_READY;
        break;
    case T3_GUIDE_READY:
        guide_ready(g,in);
        break;
    case T3_GUIDE_RECORD_WAIT:
    case T3_GUIDE_RECORDING:
        if(g->state==T3_GUIDE_RECORD_WAIT) {
            if(!in->mapped_record_key)return;
            g->state=T3_GUIDE_RECORDING;
        }
        t3_practice_keys_log(&g->ring,in->pad_word,in->side_flag);
        if(!in->select_pressed && g->ring.head!=T3_KEY_RING_WORDS-1)return;
        recording_end(g);
        g->state=T3_GUIDE_READY;
        break;
    case T3_GUIDE_STAND:
        if(!standing_ok(g,in))return;
        if(ready_to_replay(g,in))g->state=T3_GUIDE_PLAY;
        break;
    case T3_GUIDE_PLAY:
        if(!guide_play(g)){g->prompt_timer=0x60;g->prompt=T3_GUIDE_PROMPT_END;g->state=T3_GUIDE_IDLE;}
        else if(in->hit_clean || (in->move_pending && in->hold_turn)){g->prompt=T3_GUIDE_PROMPT_HIT;g->state=T3_GUIDE_IDLE;}
        else guide_move_log(g,in);
        break;
    case T3_GUIDE_REPLAY:
        if(guide_play(g))guide_move_log(g,in);
        else g->state=T3_GUIDE_REPLAY_END;
        break;
    case T3_GUIDE_REPLAY_END:
        if(in->me_cur_slot==3 || in->me_cur_slot==in->me_stance_slot) {
            g->guide_prompt=1;g->state=T3_GUIDE_IDLE;
            if(in->select_held)g->resumed_input=0;
        } else guide_move_log(g,in);
        break;
    case T3_GUIDE_MOVE_END:
        if(!in->active_first_frame)g->state=T3_GUIDE_IDLE;
        break;
    default:break;
    }
}
