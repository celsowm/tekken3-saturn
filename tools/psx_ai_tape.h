/* PRIVATE CPU-opponent check for t3_match_replay_eval: with T3_AI_TAPE (an
 * oracle case's ai.bin) and T3_AI_TABLES (psx_ai.bin), the CPU fighters'
 * pads come from psx_ai_game instead of the tape. The first AiUpdate starts
 * from the original's AI memory; each call takes the original's rand() and AI
 * generator at entry (the evaluator runs no camera, which also draws from
 * rand()). T3_AI_PIN=1 restores the original's AI memory before every call.
 * Each call's pads, and the AI memory after it, are compared with the
 * original's, and (version 2 tapes) the fighter fields and globals the AI
 * reads with the original's at entry; mismatches go to stderr.
 * T3_AI_TRACE=FRAME with T3_AI_TRACE_OUT=FILE writes that frame's bus reads
 * in fight_oracle_export.py --ai-trace's format. */
#ifndef T3_PSX_AI_TAPE_H
#define T3_PSX_AI_TAPE_H
#include "psx_ai.h"
#include "psx_ai_game.h"
#include "psx_match.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t AI_TAPE_HEAD=28u;
static t3_psx_ai_game_t ai_game;
static uint32_t ai_call_size;
static uint8_t *ai_tape,*ai_tables;
static uint32_t ai_calls,ai_next,ai_frame,ai_mismatches,ai_compared;
static const uint8_t* ai_current;
static int ai_pin,ai_started;
static FILE* ai_trace_out;
static uint32_t ai_trace_frame;
static void ai_trace(t3_psx_ai_game_t* ai,uint32_t address,unsigned size,uint32_t value) {
    (void)ai;
    if(ai_frame!=ai_trace_frame)return;
    if(size)fprintf(ai_trace_out,"R %#x %u %#x\n",address,size,value);
    else fprintf(ai_trace_out,"C %#x 0 %#x\n",address,value);
}

static uint8_t* ai_read_file(const char* path,size_t* size) {
    FILE* f=fopen(path,"rb");uint8_t* data;long n;
    if(!f || fseek(f,0,SEEK_END) || (n=ftell(f))<0 || fseek(f,0,SEEK_SET)){if(f)fclose(f);return 0;}
    data=malloc((size_t)n+1u);
    if(!data || fread(data,1,(size_t)n,f)!=(size_t)n){fclose(f);free(data);return 0;}
    fclose(f);*size=(size_t)n;return data;
}
static uint32_t ai_u32(const uint8_t* p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t ai_u16(const uint8_t* p) {return (uint16_t)(p[0]|p[1]<<8);}

/* The fighter fields AiUpdate reads, against the original's view at entry
 * (tape version 2: per fighter 0x130 bytes, seven words, two halves; then
 * the frame counter and g_fighterDistance). */
enum { AI_VIEW_FIGHTER=T3_PSX_AI_IMAGE+7u*4u+4u, AI_VIEW=2u*AI_VIEW_FIGHTER+8u };
static const struct {uint16_t off;uint8_t size;} ai_low[]={
    {0x16,2},{0x18,2},{0x1E,1},{0x1F,1},{0x2E,2},{0x30,2},{0x32,2},{0x3C,2},{0x3E,2},{0x40,2},{0x54,4},{0x58,2},
    {0x60,4},{0x64,2},{0x74,2},{0x87,1},{0xA0,2},{0xCE,1},{0xD2,1},{0xDB,1},{0xDF,1},{0xF8,4},{0x122,2}};
static void ai_field(unsigned i,unsigned off,uint32_t got,uint32_t want) {
    if(got!=want && ai_mismatches<200)
        fprintf(stderr,"ai frame %u fighter %u: field %#x is %#x, original %#x\n",ai_frame,i,off,got,want);
}
static void ai_check_view(t3_psx_ai_game_t* ai,const uint8_t* view) {
    t3_psx_match_t* m=ai->match;
    for(unsigned i=0;i<2;++i) {
        const uint8_t* v=view+AI_VIEW_FIGHTER*i;
        const t3_psx_fighter_t* f=&m->fighter[i];
        const int16_t* screen=m->config->camera?m->camera.screen_x:m->screen_x;
        /* t3_psx_ai_game_row unbinds the match; this runs inside a call. */
        const int queued=f->hit.reaction_known && f->hit.reaction.selected;
        const uint32_t pending=queued?(f->hit.prior_pending_valid?
                t3_psx_ai_game_row(ai,m,i,f->hit.prior_pending_slot):0u):
            f->vm.pending_valid?t3_psx_ai_game_row(ai,m,i,f->vm.pending_slot):0u;
        const uint32_t high[7]={pending,
            (uint32_t)f->body.push.x,(uint32_t)f->body.push.y,(uint32_t)f->body.push.z,(uint32_t)f->health_q16,
            (uint32_t)m->config->max_health_q16,(uint32_t)f->world[2].origin.y};
        static const uint16_t offs[7]={0x1A4,0x3E8,0x3EC,0x3F0,0x3F4,0x3F8,0x994};
        for(unsigned k=0;k<sizeof(ai_low)/sizeof(ai_low[0]);++k) {
            uint32_t got=0,want=0;
            for(unsigned b=0;b<ai_low[k].size;++b) {
                got|=(uint32_t)ai->image[i][ai_low[k].off+b]<<(8u*b);
                want|=(uint32_t)v[ai_low[k].off+b]<<(8u*b);
            }
            ai_field(i,ai_low[k].off,got,want);
        }
        ai->match=m;
        for(unsigned k=0;k<7;++k)ai_field(i,offs[k],high[k],ai_u32(v+T3_PSX_AI_IMAGE+4u*k));
        if(m->config->camera)ai_field(i,0x1278,(uint16_t)screen[i],ai_u16(v+T3_PSX_AI_IMAGE+28u));
        ai_field(i,0x1886,(uint8_t)ai->slot[i],v[T3_PSX_AI_IMAGE+30u]);
        ai_field(i,0x1887,(uint8_t)ai->target[i],v[T3_PSX_AI_IMAGE+31u]);
    }
    ai_field(0,T3_PSX_AI_FRAME_COUNTER,m->frame_counter,ai_u32(view+2u*AI_VIEW_FIGHTER));
    ai_field(0,T3_PSX_AI_FIGHTER_DISTANCE,m->fighter_distance,ai_u32(view+2u*AI_VIEW_FIGHTER+4u));
}

static void ai_observe(t3_psx_ai_game_t* ai,unsigned fighter,int after) {
    t3_psx_match_t* m=ai->match;
    if(!after) {
        ai_current=0;
        for(uint32_t k=ai_next;k<ai_calls;++k) {
            const uint8_t* c=ai_tape+AI_TAPE_HEAD+(size_t)k*ai_call_size;
            if(ai_u32(c)>ai_frame)break;
            if(ai_u32(c)==ai_frame && ai_u32(c+4)==fighter){ai_current=c;ai_next=k+1u;break;}
        }
        if(ai_trace_out && ai_frame==ai_trace_frame)fprintf(ai_trace_out,"A %u\n",fighter);
        if(!ai_current) {
            fprintf(stderr,"ai frame %u fighter %u: the original made no AiUpdate call\n",ai_frame,fighter);
            ++ai_mismatches;return;
        }
        if(ai_call_size>28u+T3_PSX_AI_BLOCK_SIZE)ai_check_view(ai,ai_current+28u+T3_PSX_AI_BLOCK_SIZE);
        /* Without a camera the evaluator's screen x is the previous
         * frame's; the AI reads this frame's (CameraFrame runs first). */
        if(!m->config->camera && ai_call_size>28u+T3_PSX_AI_BLOCK_SIZE)
            for(unsigned i=0;i<2;++i)m->screen_x[i]=(int16_t)ai_u16(ai_current+28u+T3_PSX_AI_BLOCK_SIZE+AI_VIEW_FIGHTER*i+T3_PSX_AI_IMAGE+28u);
        m->camera.rand=ai_u32(ai_current+8);m->camera.ai_rand=ai_u32(ai_current+12);
        if(ai_pin || !ai_started) {
            ai->self=ai_u32(ai_current+16);ai->opp=ai_u32(ai_current+20);
            memcpy(ai->block,ai_current+28,T3_PSX_AI_BLOCK_SIZE);
            ai_started=1;
        }
        return;
    }
    if(!ai_current)return;
    ++ai_compared;
    if(ai->held!=ai_u16(ai_current+24) || ai->pressed!=ai_u16(ai_current+26)) {
        if(ai_mismatches<20)fprintf(stderr,"ai frame %u fighter %u: pads %04x/%04x, original %04x/%04x\n",ai_frame,
            fighter,ai->held,ai->pressed,ai_u16(ai_current+24),ai_u16(ai_current+26));
        ++ai_mismatches;
    }
    /* The original's memory after this call is the next call's at entry. */
    if(ai_next<ai_calls) {
        const uint8_t* next=ai_tape+AI_TAPE_HEAD+(size_t)ai_next*ai_call_size+28;
        for(uint32_t o=0;o<T3_PSX_AI_BLOCK_SIZE;++o)if(ai->block[o]!=next[o]) {
            if(ai_mismatches<20)fprintf(stderr,"ai frame %u fighter %u: memory %#x is %02x, original %02x\n",ai_frame,
                fighter,T3_PSX_AI_BLOCK+o,ai->block[o],next[o]);
            ++ai_mismatches;break;
        }
    }
    if(ai->misses) {
        if(ai_mismatches<20) {
            fprintf(stderr,"ai frame %u fighter %u: %u unmapped accesses, first %#x",ai_frame,fighter,ai->misses,ai->last_miss);
            for(unsigned k=0;k<3;++k) {
                const t3_divmot_t* bank=ai->bank[k];
                const uint32_t o=ai->last_miss-ai->bank_base[k];
                if(bank && ai->last_miss>=ai->bank_base[k] && o>=bank->sections[1] && o<bank->sections[2])
                    fprintf(stderr," (bank %u row %u field %#x)",k,(o-bank->sections[1])/T3_DIVMOT_MOVE_BYTES,
                            (o-bank->sections[1])%T3_DIVMOT_MOVE_BYTES);
            }
            fputc('\n',stderr);
        }
        ai->misses=0;
    }
}

/* After the seed, with the seed RAM loaded (psx_ram_ranges.h): 1 when no tape is asked for or it is ready, 0 on error. */
static int ai_tape_open(t3_psx_match_config_t* c,t3_psx_match_t* m) {
    const char *tape=getenv("T3_AI_TAPE"),*tables=getenv("T3_AI_TABLES");
    size_t size,tables_size;
    if(!tape)return 1;
    if(!tables || !(ai_tape=ai_read_file(tape,&size)) || !(ai_tables=ai_read_file(tables,&tables_size)) ||
       size<AI_TAPE_HEAD || memcmp(ai_tape,"T3AT",4) || ai_u32(ai_tape+4)-1u>2u)return 0;
    /* Version 2 adds the view to each call, version 3 the settings. */
    ai_call_size=28u+T3_PSX_AI_BLOCK_SIZE+(ai_u32(ai_tape+4)>=2u?AI_VIEW:0u);
    if(ai_u32(ai_tape+4)>=3u)AI_TAPE_HEAD=48u;
    if(size<AI_TAPE_HEAD)return 0;
    ai_calls=ai_u32(ai_tape+24);
    if(size!=AI_TAPE_HEAD+(size_t)ai_calls*ai_call_size)return 0;
    if(!t3_psx_ai_game_init(&ai_game,ai_tables,tables_size,(uint8_t)ai_u32(ai_tape+8),0,0))return 0;
    for(unsigned k=0;k<3;++k)ai_game.bank_base[k]=ai_u32(ai_tape+12u+4u*k);
    if(AI_TAPE_HEAD==48u) {
        ai_game.game_mode=ai_u32(ai_tape+28);ai_game.attract=ai_u32(ai_tape+32);ai_game.force_layout=ai_u32(ai_tape+36);
        ai_game.difficulty=(uint16_t)ai_u32(ai_tape+40);ai_game.cpu_level=(uint8_t)ai_u32(ai_tape+44);
    }
    /* AiInitRound's record slot and target (+0x1886, +0x1887) from the seed
     * (USA fighters at 0x800A9228). */
    for(unsigned i=0;i<2;++i) {
        const uint32_t at=0x800A9228u+0x188Cu*i+0x1886u;
        if(!has(at,2u))return 0;
        ai_game.slot[i]=(int8_t)u8(at);ai_game.target[i]=(int8_t)u8(at+1u);
    }
    ai_game.observe=ai_observe;
    ai_pin=getenv("T3_AI_PIN")!=NULL;
    if(getenv("T3_AI_TRACE") && getenv("T3_AI_TRACE_OUT")) {
        ai_trace_frame=(uint32_t)strtoul(getenv("T3_AI_TRACE"),NULL,0);
        if(!(ai_trace_out=fopen(getenv("T3_AI_TRACE_OUT"),"w")))return 0;
        ai_game.trace=ai_trace;
    }
    c->ai=&ai_game;
    m->flow.human_mask=3u&~ai_game.cpu_mask;
    return 1;
}
static void ai_tape_close(void) {
    if(!ai_tape)return;
    fprintf(stderr,"ai: %u calls compared, %u mismatches\n",ai_compared,ai_mismatches);
    if(ai_trace_out)fclose(ai_trace_out);
}
#endif
