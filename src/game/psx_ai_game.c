/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Bank linking after pinned Tekken3Remake docs/research/formats/divmot-banks.md
 * and the fighter fields of docs/research/code/fighter.md. See NOTICE.md.
 *
 * The CPU opponent's bus on the owned match. The AI (psx_ai.c) reads the
 * original's memory; this shows it our state at the same addresses:
 *
 * - its own block and globals, kept here; rand() and the AI generator are
 *   the fight camera's (they are shared in the original);
 * - each fighter's fields it reads (fighter.md), taken from the typed core
 *   before the call into a small image;
 * - the motion banks at g_divmotBuffer's layout, linked as DivmotLinkBank
 *   leaves them (pointers relocated, the merged move index, the move damage
 *   with its events);
 * - the EXE tables from psx_ai.bin, the common branch rows and the built-in
 *   attack descriptors.
 *
 * Any other access reads 0 and is counted in `misses`. */
#include "psx_ai_game.h"
#include "psx_ai.h"
#include "psx_match.h"
#include "move_index.h"
#include "branch_rules.h"
#include "attack_records.h"

#define FIGHTERS T3_PSX_AI_FIGHTER_BASE
#define STRIDE T3_PSX_AI_FIGHTER_STRIDE
#define LOW T3_PSX_AI_IMAGE
#define BUILTIN 0x80017BDCu
/* the built-in attack descriptor window */
#define COMMON_ROWS T3_PSX_AI_COMMON_ROWS
#define FORCE_LAYOUT 0x800AE3D8u
#define MAX_WINDOWS 8u

/* Constant shifts: a variable one is a library call on the SH-2. */
static uint32_t le(const uint8_t* p,unsigned n) {
    uint32_t v=p[0];
    if(n>1u)v|=(uint32_t)p[1]<<8;
    if(n>2u)v|=(uint32_t)p[2]<<16;
    if(n>3u)v|=(uint32_t)p[3]<<24;
    return v;
}
static void put(uint8_t* p,unsigned n,uint32_t v) {
    p[0]=(uint8_t)v;
    if(n>1u)p[1]=(uint8_t)(v>>8);
    if(n>2u)p[2]=(uint8_t)(v>>16);
    if(n>3u)p[3]=(uint8_t)(v>>24);
}

int t3_psx_ai_game_init(t3_psx_ai_game_t* ai,const uint8_t* tables,size_t size,uint8_t cpu_mask,
                        uint16_t difficulty,uint8_t level) {
    uint32_t count;
    if(!ai || !tables || size<8u || tables[0]!='T' || tables[1]!='3' || tables[2]!='A' || tables[3]!='I' || cpu_mask>3u)return 0;
    count=le(tables+4,4);
    if(!count || count>MAX_WINDOWS || size<8u+12u*count)return 0;
    for(uint32_t k=0;k<count;++k) {
        const uint8_t* w=tables+8u+12u*k;
        const uint32_t n=le(w+4,4),at=le(w+8,4);
        if(at>size || n>size-at)return 0;
    }
    __builtin_memset(ai,0,sizeof *ai);
    ai->tables=tables;ai->tables_size=size;
    ai->cpu_mask=cpu_mask;ai->difficulty=difficulty;ai->cpu_level=level;
    for(unsigned i=0;i<3;++i)ai->slot[i]=ai->target[i]=-1;
    t3_psx_ai_game_place_banks(ai,0,1,0);
    return 1;
}

void t3_psx_ai_game_place_banks(t3_psx_ai_game_t* ai,unsigned type0,unsigned type1,int shared) {
    (void)type0;
    ai->bank_base[0]=T3_PSX_AI_BANK_BUFFER;
    ai->bank_base[1]=shared?ai->bank_base[0]:T3_PSX_AI_BANK_BUFFER+(type1==3u?0x5233Cu:0x62958u);
    ai->bank_base[2]=T3_PSX_AI_BANK_BUFFER+0xB4C94u;
}

/* --- banks ------------------------------------------------------------------------------- */

static uint32_t base_of(const t3_psx_ai_game_t* b,const t3_divmot_t* bank) {
    for(unsigned k=0;k<3;++k)if(b->bank[k]==bank)return b->bank_base[k];
    return 0;
}
/* The linked index entry: a pointer to the row. */
static uint32_t row_address(const t3_psx_ai_game_t* b,const t3_divmot_t* bank,unsigned slot) {
    const t3_divmot_t* source;uint32_t id;
    if(!t3_move_index_entry(bank,slot,&source,&id))return 0;
    if(id>=0x40000000u) {
        const t3_divmot_t* common=b->bank[2];
        return common?b->bank_base[2]+common->sections[1]+T3_DIVMOT_MOVE_BYTES*(id-0x40000000u):0;
    }
    return base_of(b,source)+source->sections[1]+T3_DIVMOT_MOVE_BYTES*id;
}
static uint32_t descriptor_address(const t3_psx_ai_game_t* b,const t3_divmot_t* bank,uint32_t d) {
    if(!d || (d>=0x7FF0u && d<=0x7FFBu)) {
        const uint8_t* w=bank->builtin_attacks;
        if(!w)return 0;
        for(unsigned i=0;i<T3_BUILTIN_ATTACK_COUNT;++i) {
            const uint8_t* e=w+T3_BUILTIN_ATTACK_TABLE+8u*i;
            if(le(e,4)==d)return BUILTIN+le(e+4,4);
        }
        return 0;
    }
    return base_of(b,bank)+bank->sections[9]+d-4u;
}
/* A word of a move row as linked; `known` 0 for the words the AI never reads. */
static uint32_t row_word(t3_psx_ai_game_t* b,const t3_divmot_t* bank,const uint8_t* row,uint32_t field,int* known) {
    const uint32_t raw=le(row+field,4);
    *known=1;
    switch(field) {
    case 0x0C:return base_of(b,bank)+bank->sections[0]+T3_DIVMOT_BRANCH_BYTES*raw;
    case 0x14:return (raw&0xFFFF0000u)|t3_move_total_damage(bank,(uint16_t)raw,le(row+0x20,4));
    case 0x28:return descriptor_address(b,bank,raw);
    case 0x00:case 0x18:case 0x1C:case 0x20:*known=0;return raw;
    default:return raw;
    }
}
/* The linked bank word at aligned offset `o` (address `at` for a miss). */
static uint32_t bank_word(t3_psx_ai_game_t* b,const t3_divmot_t* bank,uint32_t base,uint32_t o,uint32_t first,uint32_t at) {
    if(o>=4u && o<4u+4u*T3_DIVMOT_SECTIONS)return base+bank->sections[(o-4u)/4u];
    if(o>=bank->sections[1] && o<bank->sections[2]) {
        const uint32_t r=(o-bank->sections[1])/T3_DIVMOT_MOVE_BYTES,k=(o-bank->sections[1])%T3_DIVMOT_MOVE_BYTES;
        int known;
        const uint32_t v=row_word(b,bank,bank->data+bank->sections[1]+r*T3_DIVMOT_MOVE_BYTES,k,&known);
        /* +0x18's first byte is overwritten at load; +0x19..+0x1B stay. */
        if(!known && k==0x18u && first!=o)known=1;
        if(!known && b->misses++==0)b->last_miss=at;
        return v;
    }
    if(bank->bank_type!=99u && o>=bank->sections[2] && o<bank->sections[3])
        return row_address(b,bank,(o-bank->sections[2])/4u);
    uint32_t v=0;
    for(unsigned i=0;i<4u;++i)v|=(o+i<bank->size?(uint32_t)bank->data[o+i]:0u)<<(8u*i);
    return v;
}
static int bank_read(t3_psx_ai_game_t* b,uint32_t address,unsigned size,uint32_t* out) {
    for(unsigned k=0;k<3;++k) {
        const t3_divmot_t* bank=b->bank[k];
        const uint32_t base=b->bank_base[k];
        if(!bank || address<base || address-base>=bank->size)continue;
        const uint32_t o=address-base,shift=8u*(o&3u);
        /* The linked words are built once per aligned word, not once per byte. */
        uint32_t v=bank_word(b,bank,base,o&~3u,o,address)>>shift;
        if((o&3u)+size>4u)v|=bank_word(b,bank,base,(o&~3u)+4u,(o&~3u)+4u,address)<<(32u-shift);
        *out=size==4u?v:v&((1u<<(8u*size))-1u);
        return 1;
    }
    return 0;
}

/* --- fighters ---------------------------------------------------------------------------- */

static void fill_image(t3_psx_ai_game_t* b,unsigned i) {
    const t3_psx_match_t* m=b->match;
    const t3_psx_fighter_t* f=&m->fighter[i];
    const t3_psx_ground_state_t* g=&f->ground;
    uint8_t* p=b->image[i];
    /* Init zeroed the image and every field below is written each call. */
    put(p+0x16,2,b->bank[i]->bank_type);
    put(p+0x18,2,m->config->character[i]);
    p[0x1E]=(uint8_t)i;p[0x1F]=(uint8_t)(1u-i);
    put(p+0x2E,2,g->heading_delta);put(p+0x30,2,g->relative_angle);put(p+0x32,2,(uint32_t)g->facing_quadrant);
    put(p+0x3C,2,g->opp_heading_delta);put(p+0x3E,2,g->opp_relative_angle);
    put(p+0x40,2,(uint32_t)g->opponent_quadrant);
    const int ko=f->hit.collapsed!=0;
    put(p+0x54,4,row_address(b,b->bank[i],ko?f->hit.prior_slot:f->vm.slot));
    put(p+0x58,2,ko?f->hit.prior_pose_frame:(uint32_t)f->vm.clock.pose_frame);
    put(p+0x60,4,g->state);
    put(p+0x64,2,ko?f->hit.prior_attack_word:f->vm.move.attack_word);
    put(p+0x74,2,(uint32_t)g->throw_state);
    p[0x87]=g->was_hit_this_move;
    put(p+0xA0,2,ko?f->hit.prior_slot:f->vm.slot);
    p[0xCE]=f->got_hit;p[0xD2]=f->whiffed;p[0xDB]=g->in_air;p[0xDF]=g->about_to_hit;
    put(p+0xF8,4,g->distance);
    put(p+0x122,2,(uint32_t)g->power_timer);
}
/* A fighter field above the image, as a word containing `off`. */
static int fighter_high(const t3_psx_ai_game_t* b,unsigned i,uint32_t off,uint32_t* word) {
    const t3_psx_fighter_t* f=&b->match->fighter[i];
    switch(off&~3u) {
    case 0x1A4:
        /* HitApply drops the queued row; the original queues the reaction in
         * MoveStep, after InputSource, where the match already has it. */
        if(f->hit.reaction_known && f->hit.reaction.selected) {
            /* The reaction is queued in MoveStep; the pre-hit row is still there. */
            *word=f->hit.prior_pending_valid?
                  row_address(b,b->bank[i],f->hit.prior_pending_slot):0;
            return 1;
        }
        *word=f->vm.pending_valid?row_address(b,b->bank[i],f->vm.pending_slot):0;return 1;
    case 0x3E8:*word=(uint32_t)f->body.push.x;return 1;
    case 0x3EC:*word=(uint32_t)f->body.push.y;return 1;
    case 0x3F0:*word=(uint32_t)f->body.push.z;return 1;
    case 0x3F4:*word=(uint32_t)f->health_q16;return 1;
    case 0x3F8:*word=(uint32_t)b->match->config->max_health_q16;return 1;
    case 0x994:*word=(uint32_t)f->world[2].origin.y;return 1;
    case 0x1278: {
        /* The camera's, or without one the frames' (as screen_of in psx_match.c). */
        const t3_psx_match_t* m=b->match;
        *word=(uint16_t)(m->config->camera?m->camera.screen_x[i]:m->screen_x[i]);return 1;
    }
    case 0x1884:*word=(uint32_t)(uint8_t)b->slot[i]<<16|(uint32_t)(uint8_t)b->target[i]<<24;return 1;
    default:return 0;
    }
}

/* --- the bus ----------------------------------------------------------------------------- */

static uint32_t* global_word(t3_psx_ai_game_t* b,uint32_t address) {
    t3_psx_ai_game_t* ai=b;
    t3_psx_fight_camera_t* camera=(t3_psx_fight_camera_t*)&b->match->camera;
    switch(address) {
    case T3_PSX_AI_SELF:return &ai->self;
    case T3_PSX_AI_OPP:return &ai->opp;
    case T3_PSX_AI_RAND:return &camera->rand;
    case T3_PSX_AI_LCG:return &camera->ai_rand;
    case T3_PSX_AI_GAME_MODE:return &ai->game_mode;
    case T3_PSX_AI_TEAM_OR_SURVIVAL:return &ai->attract;
    case FORCE_LAYOUT:return &ai->force_layout;
    default:return 0;
    }
}
static int table_read(const t3_psx_ai_game_t* ai,uint32_t address,unsigned size,uint32_t* out) {
    const uint32_t count=le(ai->tables+4,4);
    for(uint32_t k=0;k<count;++k) {
        const uint8_t* w=ai->tables+8u+12u*k;
        const uint32_t start=le(w,4),n=le(w+4,4);
        if(address>=start && address-start+size<=n) {*out=le(ai->tables+le(w+8,4)+(address-start),size);return 1;}
    }
    return 0;
}
static uint32_t read_raw(void* user,uint32_t address,unsigned size) {
    t3_psx_ai_game_t* b=user;
    t3_psx_ai_game_t* ai=b;
    uint32_t v,*g;
    /* The linked banks sit above every other mapped range. */
    if(address>=T3_PSX_AI_BANK_BUFFER) {
        const uint32_t key=(address&0x1FFFFFFFu)|(uint32_t)size<<29;
        t3_psx_ai_memo_t* m=b->memo+2u*((address^address>>8^size)&(T3_PSX_AI_MEMO/2u-1u)); /* as rd() in psx_ai.c */
        if(m[0].key==key)return m[0].value;
        if(m[1].key==key)return m[1].value;
        if(bank_read(b,address,size,&v)) {m[1]=m[0];m[0]=(t3_psx_ai_memo_t){key,v};return v;}
    }
    if(address-T3_PSX_AI_BLOCK<T3_PSX_AI_BLOCK_SIZE && address-T3_PSX_AI_BLOCK+size<=T3_PSX_AI_BLOCK_SIZE)
        return le(ai->block+(address-T3_PSX_AI_BLOCK),size);
    if(address-FIGHTERS<3u*STRIDE) {
        const unsigned i=(address-FIGHTERS)/STRIDE;
        const uint32_t off=(address-FIGHTERS)%STRIDE;
        uint32_t word;
        if(i<2u && off+size<=LOW)return le(b->image[i]+off,size);
        if(i<2u && fighter_high(b,i,off,&word) && (off&3u)+size<=4u)return word>>(8u*(off&3u))&(size==4u?~0u:(1u<<(8u*size))-1u);
        if(i==2u && (off==0x1886u || off==0x1887u) && size==1u)
            return (uint8_t)(off==0x1886u?ai->slot[2]:ai->target[2]);
    } else if(b->bank[0]->builtin_attacks && address-BUILTIN<T3_BUILTIN_ATTACK_BYTES &&
            address-BUILTIN+size<=T3_BUILTIN_ATTACK_BYTES)
        return le(b->bank[0]->builtin_attacks+(address-BUILTIN),size);
    else if((g=global_word(b,address&~3u))!=0 && (address&3u)+size<=4u)
        return *g>>(8u*(address&3u))&(size==4u?~0u:(1u<<(8u*size))-1u);
    else if(address==T3_PSX_AI_DIFFICULTY && size==2u)return ai->difficulty;
    else if(address==T3_PSX_AI_CPU_LEVEL && size==1u)return ai->cpu_level;
    else if(address==T3_PSX_AI_FRAME_COUNTER && size==4u)return b->match->frame_counter;
    else if(address==T3_PSX_AI_FIGHTER_DISTANCE && size==4u)return b->match->fighter_distance;
    else if((address==T3_PSX_AI_BANKS || address==T3_PSX_AI_BANKS+4u) && size==4u)
        return ai->bank_base[(address-T3_PSX_AI_BANKS)/4u];
    else if(bank_read(b,address,size,&v))return v;
    else if(address-COMMON_ROWS<12u*b->bank[0]->common_branch_count &&
            address-COMMON_ROWS+size<=12u*b->bank[0]->common_branch_count)
        return le(b->bank[0]->common_branches+(address-COMMON_ROWS),size);
    else if(table_read(ai,address,size,&v))return v;
    if(ai->misses++==0)ai->last_miss=address;
    return 0;
}
static uint32_t bus_read(void* user,uint32_t address,unsigned size) {
    t3_psx_ai_game_t* ai=user;
    const uint32_t v=read_raw(user,address,size);
    ++ai->reads;
    if(ai->trace)ai->trace(ai,address,size,v);
    return v;
}
static void bus_write(void* user,uint32_t address,unsigned size,uint32_t value) {
    t3_psx_ai_game_t* b=user;
    t3_psx_ai_game_t* ai=b;
    uint32_t* g;
    if(address-T3_PSX_AI_BLOCK<T3_PSX_AI_BLOCK_SIZE && address-T3_PSX_AI_BLOCK+size<=T3_PSX_AI_BLOCK_SIZE) {
        put(ai->block+(address-T3_PSX_AI_BLOCK),size,value);
        return;
    }
    if(address-FIGHTERS<3u*STRIDE && size==1u) {
        const unsigned i=(address-FIGHTERS)/STRIDE;
        const uint32_t off=(address-FIGHTERS)%STRIDE;
        if(off==0x1886u){ai->slot[i]=(int8_t)value;return;}
        if(off==0x1887u){ai->target[i]=(int8_t)value;return;}
    }
    if((g=global_word(b,address))!=0 && size==4u){*g=value;return;}
    if(ai->misses++==0)ai->last_miss=address;
}
/* A read the windows cover (they are empty while a trace listens), else the bus. */
static uint32_t window_read(t3_psx_ai_game_t* ai,uint32_t address,unsigned size) {
    const t3_psx_ai_window_t* w=ai->window;
    for(unsigned k=ai->window_count;k;--k,++w)
        if(address-w->base<w->size && size<=w->size-(address-w->base))return le(w->data+(address-w->base),size);
    return bus_read(ai,address,size);
}
static int bus_condition(void* user,uint32_t row,uint32_t self,uint32_t opp) {
    t3_psx_ai_game_t* b=user;
    const unsigned i=(self-FIGHTERS)/STRIDE;
    (void)opp;
    int r;
    if(i>1u)return 0;
    ++b->conditions;
    /* Most calls test no fighter fact; the match does not change during one. */
    if(!(b->facts_ready>>i&1u)) {
        t3_psx_match_branch_facts(b->match,i,&b->facts[i]);
        b->facts_ready|=1u<<i;
    }
    r=t3_branch_condition_matches((uint8_t)window_read(b,row+3u,1),(uint16_t)window_read(b,row+4u,2),&b->facts[i]);
    if(b->trace)b->trace(b,row,0,(uint32_t)r);
    return r;
}

static void bind(t3_psx_ai_game_t* ai,t3_psx_match_t* match) {
    const t3_psx_match_config_t* c=match->config;
    ai->match=match;
    ai->bank[0]=t3_psx_match_bank(c,0);ai->bank[1]=t3_psx_match_bank(c,1);ai->bank[2]=c->common;
    int same=1;
    for(unsigned k=0;k<3;++k)same&=ai->memo_bank[k]==ai->bank[k] && ai->memo_base[k]==ai->bank_base[k];
    if(!same) {
        /* Key 0 never matches: every key carries a non-zero size. */
        __builtin_memset(ai->memo,0,sizeof ai->memo);
        __builtin_memset(ai->pick,0,sizeof ai->pick);
        ai->collect.valid=0;
        __builtin_memcpy(ai->memo_bank,ai->bank,sizeof ai->bank);
        __builtin_memcpy(ai->memo_base,ai->bank_base,sizeof ai->bank_base);
    }
}
/* A word the bus maps (unaligned high fields stay on the bus); a colliding one stays on the bus too. */
static void add_word(t3_psx_ai_game_t* ai,uint32_t address,uint32_t* host) {
    t3_psx_ai_word_t* e=&ai->word[(address>>2^address>>9)&63u];
    if(host && !(address&3u) && !e->address)*e=(t3_psx_ai_word_t){address,host};
}
static uint32_t* word_at(t3_psx_ai_game_t* ai,uint32_t address) {
    t3_psx_ai_word_t* e=&ai->word[(address>>2^address>>9)&63u];
    return e->address==address?e->host:0;
}
static int open_bus(t3_psx_ai_game_t* ai,t3_psx_match_t* match,t3_psx_ai_bus_t* bus) {
    if(!ai || !match || !ai->tables || !match->config->common)return 0;
    bind(ai,match);
    for(unsigned i=0;i<2;++i)fill_image(ai,i);
    ai->facts_ready=0;
    ai->window_count=0;
    if(!ai->trace) {
        static const uint16_t high[9]={0x1A4,0x3E8,0x3EC,0x3F0,0x3F4,0x3F8,0x994,0x1278,0x1884};
        static const uint32_t globals[7]={T3_PSX_AI_SELF,T3_PSX_AI_OPP,T3_PSX_AI_RAND,T3_PSX_AI_LCG,
            T3_PSX_AI_GAME_MODE,T3_PSX_AI_TEAM_OR_SURVIVAL,FORCE_LAYOUT};
        /* The table's places are fixed for a match (two globals live in its camera). */
        if(ai->word_match!=match) {
            __builtin_memset(ai->word,0,sizeof ai->word);
            for(unsigned k=0;k<7;++k)add_word(ai,globals[k],global_word(ai,globals[k]));
            for(unsigned i=0;i<2;++i)
                for(unsigned k=0;k<9;++k)add_word(ai,FIGHTERS+STRIDE*i+high[k],&ai->high[i][k]);
            ai->word_match=match;
        }
        for(unsigned i=0;i<2;++i)
            for(unsigned k=0;k<9;++k)(void)fighter_high(ai,i,high[k],&ai->high[i][k]);
        const t3_divmot_t* b0=ai->bank[0];
        /* Most-read first; the block itself is the bus's ram (written only through put()). */
        for(unsigned i=0;i<2;++i)
            ai->window[ai->window_count++]=(t3_psx_ai_window_t){FIGHTERS+STRIDE*i,LOW,ai->image[i]};
        if(b0->common_branches && b0->common_branch_count)
            ai->window[ai->window_count++]=(t3_psx_ai_window_t){COMMON_ROWS,12u*b0->common_branch_count,b0->common_branches};
        for(unsigned k=0;k<3;++k) {
            const t3_divmot_t* bank=ai->bank[k];
            if(!bank || (k==1u && bank==ai->bank[0]) || bank->sections[1]<=bank->sections[0])continue;
            ai->window[ai->window_count++]=(t3_psx_ai_window_t){ai->bank_base[k]+bank->sections[0],
                bank->sections[1]-bank->sections[0],bank->data+bank->sections[0]};
        }
        if(b0->builtin_attacks)
            ai->window[ai->window_count++]=(t3_psx_ai_window_t){BUILTIN,T3_BUILTIN_ATTACK_BYTES,b0->builtin_attacks};
    }
    *bus=(t3_psx_ai_bus_t){ai,bus_read,bus_write,bus_condition,0,ai->window,ai->window_count,
                           ai->trace?0:ai->block,T3_PSX_AI_BLOCK,ai->trace?0:T3_PSX_AI_BLOCK_SIZE,
                           ai->trace?0:ai->memo,T3_PSX_AI_MEMO/2u-1u,T3_PSX_AI_BANK_BUFFER,
                           ai->trace?0:ai->word,63u,
                           ai->trace?0:ai->pick,127u,&ai->pick_index,ai->trace?0:&ai->collect};
    return 1;
}

/* The slot and target bytes the call wrote through the word table. */
static void close_bus(t3_psx_ai_game_t* ai,const t3_psx_ai_bus_t* bus) {
    for(unsigned i=0;i<2;++i)
        if(bus->words && word_at(ai,FIGHTERS+STRIDE*i+0x1884u)==&ai->high[i][8]) {
            ai->slot[i]=(int8_t)(ai->high[i][8]>>16);
            ai->target[i]=(int8_t)(ai->high[i][8]>>24);
        }
    ai->match=0;
}
int t3_psx_ai_game_init_round(t3_psx_ai_game_t* ai,t3_psx_match_t* match) {
    t3_psx_ai_bus_t bus;
    if(!open_bus(ai,match,&bus))return 0;
    t3_psx_ai_init_round(&bus);
    close_bus(ai,&bus);
    return 1;
}

t3_input_t t3_psx_ai_game_pad(uint16_t held,uint16_t pressed) {
    static const uint16_t buttons[4][2]={{0x80u,T3_IN_LP},{0x10u,T3_IN_RP},{0x40u,T3_IN_LK},{0x20u,T3_IN_RK}};
    t3_input_t r={0};
    /* FUN_8002C7D4 with side 0: 0x2000 is towards the opponent. */
    if(!(held&0x2000u)!=!(held&0x8000u))r.held|=(held&0x2000u)?T3_IN_FORWARD:T3_IN_BACK;
    if(!(held&0x1000u)!=!(held&0x4000u))r.held|=(held&0x1000u)?T3_IN_LEFT:T3_IN_RIGHT;
    for(unsigned k=0;k<4;++k) {
        if(held&buttons[k][0])r.held|=buttons[k][1];
        if(pressed&buttons[k][0])r.pressed|=buttons[k][1];
    }
    return r;
}

int t3_psx_ai_game_input(t3_psx_ai_game_t* ai,t3_psx_match_t* match,unsigned i,t3_input_t* out) {
    t3_psx_ai_bus_t bus;uint16_t held,pressed;
    if(i>1u || !out || !open_bus(ai,match,&bus))return 0;
    /* Without health the original pushes uninitialised stack words. */
    if(!match->fighter[i].health_q16) {ai->match=0;*out=ai->pad[i]=(t3_input_t){0};return 1;}
    if(ai->observe)ai->observe(ai,i,0);
    t3_psx_ai_update(&bus,FIGHTERS+STRIDE*i,0,0,&held,&pressed);
    ai->held=held;ai->pressed=pressed;
    if(ai->observe)ai->observe(ai,i,1);
    close_bus(ai,&bus);
    *out=ai->pad[i]=t3_psx_ai_game_pad(held,pressed);
    return 1;
}

uint32_t t3_psx_ai_game_row(t3_psx_ai_game_t* ai,t3_psx_match_t* match,unsigned i,unsigned slot) {
    uint32_t row;
    bind(ai,match);
    row=row_address(ai,ai->bank[i],slot);
    ai->match=0;
    return row;
}
