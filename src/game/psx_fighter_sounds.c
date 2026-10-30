/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted rules: pinned Tekken3Remake fighter_sounds.gd / sound.md,
 * FighterSounds 0x80041138 and its helpers. Changes: portable bounded table
 * reader, explicit facts/state and output callback; no PS1 audio execution.
 * See NOTICE.md. */
#include "psx_fighter_sounds.h"

typedef struct { const t3_psx_sound_context_t* c;int ok; } reader;
static uint32_t readn(reader* r,uint32_t at,unsigned n) {
    const t3_psx_sound_tables_t* t=&r->c->tables;
    if(at>t->size || n>t->size-at){r->ok=0;return 0;}
    uint32_t value=0;
    for(unsigned i=0;i<n;++i)value|=(uint32_t)t->data[at+i]<<(8*i);
    return value;
}
static uint32_t r32(reader* r,uint32_t at){return readn(r,at,4);}
static uint16_t r16(reader* r,uint32_t at){return (uint16_t)readn(r,at,2);}
static int16_t s16(reader* r,uint32_t at){return (int16_t)r16(r,at);}
#if defined(T3_SOUND_RAND_LOG)
/* Diagnostic: frame counter and source line of every draw (PS1 comparison). */
volatile uint32_t g_t3_sound_rand_log[128][2],g_t3_sound_rand_count;
#define random_next(r) random_next_at((r),__LINE__)
static uint32_t random_next_at(reader* r,unsigned line) {
    const uint32_t n=g_t3_sound_rand_count++;
    g_t3_sound_rand_log[n&127u][0]=r->c->frame_counter;g_t3_sound_rand_log[n&127u][1]=line;
#else
static uint32_t random_next(reader* r) {
#endif
    uint32_t* value=r->c->random;
    *value=*value*0x41C64E6Du+0x3039u;
    return (*value>>16)&0x7FFFu;
}
static void emit(reader* r,unsigned kind,unsigned p,uint16_t code) {
    if(r->c->emit)r->c->emit(r->c->user,kind,p,code);
}
int t3_psx_sound_tables_valid(t3_psx_sound_tables_t t) {
    if(!t.data || t.size<24 || t.data[0]!='T' || t.data[1]!='3' ||
       t.data[2]!='S' || t.data[3]!='F')return 0;
    const t3_psx_sound_context_t c={.tables=t};reader r={&c,1};
    if(r32(&r,4)!=1 || r16(&r,8)!=23 || r16(&r,10)!=470)return 0;
    const uint32_t voice=r32(&r,12),script=r32(&r,16),aux=r32(&r,20);
    if(voice>t.size || 23*24>t.size-voice || script>t.size ||
       470*4>t.size-script || aux>t.size || 20>t.size-aux)return 0;
    return r.ok;
}
void t3_psx_sound_script_stop(t3_psx_sound_state_t* s) {
    s->script=-1;s->last_script=s->position=s->frame=0;
}
int t3_psx_sound_voice_code(t3_psx_sound_tables_t tables,unsigned voice,uint16_t id,uint16_t* code) {
    if(!code || voice>=23 || !t3_psx_sound_tables_valid(tables))return -1;
    unsigned category=id>>12;if(category<2 || category>5)return 0;
    const t3_psx_sound_context_t c={.tables=tables};reader r={&c,1};
    uint32_t list=r32(&r,r32(&r,12)+24*voice),index=id&0xFFF;
    unsigned cat=category-2;
    int count=s16(&r,list+2*cat);if(count<0)return -1;
    if(index>=(unsigned)count)return r.ok?0:-1;
    for(unsigned i=0;i<cat;++i){int n=s16(&r,list+2*i);if(n<0)return -1;index+=(unsigned)n;}
    uint16_t result=r16(&r,list+8+2*index);if(!r.ok)return -1;
    *code=result;return 1;
}
static uint32_t voice_record(reader* r,unsigned voice) {
    if(voice>=23){r->ok=0;return 0;}
    return r32(r,12)+voice*24;
}
static void special(reader* r,t3_psx_sound_state_t s[2],
                    const t3_psx_sound_facts_t f[2],unsigned p,unsigned n) {
    const unsigned q=1u-p;
    switch(n) {
    case 0:s[p].hold=1;break;
    case 1:
        if(s[q].script!=-1) {
            emit(r,T3_SOUND_VOICE,f[p].player,r16(r,voice_record(r,f[p].voice_set)+18));
            t3_psx_sound_script_stop(&s[q]);
        }
        break;
    case 2:s[p].voice_cooldown=1;break;
    case 3: {
        const unsigned index=((random_next(r)&255)*3)>>8;
        emit(r,T3_SOUND_EFFECT,f[q].player,r16(r,r32(r,20)+14+2*index));break;
    }
    default:break;
    }
}
static void impact(reader* r,t3_psx_sound_state_t s[2],
                   const t3_psx_sound_facts_t f[2],unsigned p,uint16_t code) {
    const unsigned q=1u-p;const int wooden=f[q].character==15 && f[q].wooden;
    if(f[q].guarded) {
        code=wooden?(f[p].first_joint<12?0x707B:0x707A):(f[p].first_joint<12?0x704D:0x704E);
        emit(r,T3_SOUND_EFFECT,f[q].player,code);return;
    }
    t3_psx_sound_script_stop(&s[q]);
    if(f[q].character==16) {
        code=f[p].damage<7 || f[q].damage<16?0x705D:
             (r->c->vblank&3?0x7098:0x7099);
        emit(r,T3_SOUND_EFFECT,f[q].player,code);
    } else if(wooden) {
        const unsigned index=((random_next(r)&255)*5)>>8;
        emit(r,T3_SOUND_EFFECT,f[q].player,r16(r,r32(r,20)+2*index));
    } else {
        if(code==0x7000 || code==0x7041 || code==0x7042 || code==0x7045) {
            const unsigned heavy=f[p].first_joint>=12;
            uint32_t random=0;
            if(!heavy)random=random_next(r);
            if(f[p].damage<13)code=heavy?0x7045:r16(r,r32(r,20)+10+2*((random>>7)&1));
            else if(f[p].damage<23)code=heavy?0x7046:0x7042;
            else if(f[p].damage<33)code=heavy?0x7047:0x7043;
            else code=heavy?0x7048:0x7044;
        }
        emit(r,T3_SOUND_EFFECT,f[p].player,code);
    }
}
static void ambient(reader* r,t3_psx_sound_state_t* s,const t3_psx_sound_facts_t* f,
                    const t3_psx_sound_facts_t* opp,uint32_t voice,int voiced) {
    if(!voiced && f->health && f->hit_clean && !s->damage_voice_timer && (r->c->frame_counter&1)) {
        const uint32_t list=r32(r,voice+8);
        const int heavy=opp->damage>=20;
        const int count=s16(r,list+2u*(unsigned)heavy);
        const int first=heavy?s16(r,list):0;
        voiced=1;
        if(count) {
            if(count<0 || first<0){r->ok=0;return;}
            const unsigned index=((random_next(r)&255)*(unsigned)count)>>8;
            emit(r,T3_SOUND_VOICE,f->player,r16(r,list+4+2*((unsigned)first+index)));
            s->damage_voice_timer=40;
        }
    }
    if(!s->voice_cooldown && !voiced && f->damage && !f->throw_state && f->move_changed &&
       !(random_next(r)&3)) {
        const uint32_t list=r32(r,voice+4);
        const int count=s16(r,list);
        if(count<0){r->ok=0;return;}
        const unsigned index=((random_next(r)&255)*(unsigned)count)>>8;
        emit(r,T3_SOUND_VOICE,f->player,r16(r,list+2+2*index));
        s->voice_cooldown=s16(r,voice+12);
    }
    if(!s->hold) {
        if(f->pose_frame==f->air_first && f->pose_frame!=1)emit(r,T3_SOUND_EFFECT,f->player,0x1053);
        if(f->landed_a)emit(r,T3_SOUND_EFFECT,f->player,0x1054);
        if(f->landed_b)emit(r,T3_SOUND_EFFECT,f->player,0x104F);
    }
    if(f->pose_frame==(int)f->active_first-1)emit(r,T3_SOUND_EFFECT,f->player,0x604A);
}
static void script(reader* r,t3_psx_sound_state_t s[2],
                   const t3_psx_sound_facts_t f[2],unsigned p) {
    t3_psx_sound_state_t* a=&s[p];
    if(a->script<0 || a->script>=470 || a->position<0){r->ok=0;return;}
    const uint32_t list=r32(r,r32(r,16)+4u*(unsigned)a->script);
    a->frame=(int16_t)((uint16_t)a->frame+1u);
    for(unsigned n=0;n<4096 && r->ok;++n) {
        const uint32_t word=r32(r,list+4u*(unsigned)a->position);
        const unsigned frame=word&0xFFF;
        if(!frame){t3_psx_sound_script_stop(a);return;}
        if(a->frame<(int)frame)return;
        if(a->frame==(int)frame) {
            const unsigned type=(word>>12)&15;const uint16_t code=(uint16_t)(word>>16);
            if(type==0)emit(r,T3_SOUND_SYSTEM,0,code);
            else if(type==1 || type==2) {
                const unsigned q=type==1?p:1u-p;
                const unsigned category=code>>12;
                emit(r,category>=2 && category<=5?T3_SOUND_VOICE:T3_SOUND_EFFECT,f[q].player,code);
            } else if(type==3)special(r,s,f,p,code&0xFFF);
        }
        a->position=(int16_t)((uint16_t)a->position+1u);
        if(a->position<0){r->ok=0;return;}
    }
    r->ok=0;
}
int t3_psx_fighter_sounds(t3_psx_sound_state_t s[2],const t3_psx_sound_facts_t f[2],
                         unsigned p,const t3_psx_sound_context_t* c) {
    if(!s || !f || !c || !c->random || p>1 || !t3_psx_sound_tables_valid(c->tables))return 0;
    reader r={c,1};const unsigned q=1u-p;
    if(f[p].player>1 || f[q].player>1 || f[p].command_bytes>6 ||
       (f[p].command_bytes && !f[p].commands) || f[p].command_bytes%2)return 0;
    const uint32_t voice=voice_record(&r,f[p].voice_set);
    s[p].hold=0;
    if(f[p].hit_clean || (f[p].throw_state<0 && f[p].pose_frame==1))
        emit(&r,T3_SOUND_VOICE_OFF,f[p].player,0);
    int voiced=0,impacted=0;
    if(f[p].ko) {
        const uint32_t list=r32(&r,voice);
        const int count=s16(&r,list+4);
        if(count<0)return 0;
        const uint16_t id=(uint16_t)((((random_next(&r)&255)*(unsigned)count)>>8)|0x4000);
        emit(&r,T3_SOUND_VOICE,f[p].player,id);voiced=1;
    }
    if(!f[q].health && f[q].hit_clean) {
        emit(&r,T3_SOUND_EFFECT,f[p].player,f[p].first_joint<11?0x7044:0x7048);impacted=1;
    }
    for(size_t k=0;k<f[p].command_bytes;k+=2) {
        const uint16_t code=(uint16_t)(f[p].commands[k]|((uint16_t)f[p].commands[k+1]<<8));
        if(code==0xFFFF)break;
        switch(code>>12) {
        case 1:case 7:if(f[p].contact && !impacted)impact(&r,s,f,p,code);break;
        case 2:case 3:
            if(f[p].pose_frame==1 && f[p].health && !s[p].voice_cooldown && !voiced) {
                emit(&r,T3_SOUND_VOICE,f[p].player,code);
                if((code>>12)==2)s[p].voice_cooldown=s16(&r,voice+12);
                else voiced=1;
            }
            break;
        case 4:case 5:emit(&r,T3_SOUND_EFFECT,f[p].player,code);break;
        case 8:
            if((code&0xFFF) && s[p].last_script!=(int)(code&0xFFF)) {
                s[p].script=s[p].last_script=(int16_t)(code&0xFFF);s[p].position=0;
                s[p].frame=(int16_t)((uint16_t)f[p].pose_frame-1u);
            }
            break;
        case 9:special(&r,s,f,p,code&0xFFF);break;
        default:break;
        }
    }
    if(f[p].got_hit)t3_psx_sound_script_stop(&s[p]);
    if(s[p].script!=-1 && (c->camera_phase<=2 || c->camera_phase==5))script(&r,s,f,p);
    else ambient(&r,&s[p],&f[p],&f[q],voice,voiced);
    if(s[p].voice_cooldown>0)--s[p].voice_cooldown;
    if(s[p].damage_voice_timer>0)--s[p].damage_voice_timer;
    return r.ok;
}
