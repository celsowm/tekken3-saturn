/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of the CPU opponent in pinned Tekken3Remake
 * tools/research/ai_sim.py and ai_update.py (AiUpdate 0x80059B78, Japan
 * Rev.1). Changes: guest memory goes through a caller's bus, the decision's
 * early exits are return values, and Tekken Ball (mode 7, volley overlay
 * projection) is left out. Record offsets: docs/research/code/ai.md. */
#include "psx_ai.h"
#include <stddef.h>

#define END 0xC000u
#define COMMON 0xC00Cu
#define SIDE_STEP 0xC001u
#define SIDE_STEP_ALT 0xC002u
#define HINT_NEVER 0x400000u
#define GUARD_HIGH 0x1052u
#define GUARD_LOW 0x2829u
#define FILTER_NO_LOWS 0x800616B4u
#define BAND_REACH 0x80098610u
#define APPROACH_REACH 0x80098628u
#define NUMPAD_PAD 0x80023288u
#define DIRECTION_MASKS 0x800232A8u
#define DIRECTION_PAD 0x800232BCu
#define BUTTON_PAD 0x800232D0u
#define SIDE_STEP_SCRIPTS 0x800232DAu
#define SEQ_C001 0x8001006Cu
#define SEQ_C002 0x80010078u
#define INPUT_SEQ_A 0x800958F4u
#define INPUT_SEQ_B 0x800959F0u
#define IDLE_SCRIPT 0x80022EB2u
#define WAKE_UP_SCRIPT 0x80022E96u
#define FORCE_TARGETS 0x8009F6A8u
#define HOOK_TABLE 0x800233A0u
#define KING_THROWS 0x8002334Au
#define KING_VS_CROUCH 0x8002332Eu
#define KING_CLOSE 0x80023312u
#define OGRE_ESCAPES 0x80023308u
#define AI_PARAMS 0x80023418u
#define COMMON_PARAMS 0x8002408Eu
#define BAND_TABLE 0x80098640u
#define FORCE_WORDS 0x80023210u
#define FORCE_LAYOUT 0x800AE3D8u
#define ACTION_TABLE 0x800230E0u
#define GUARD_MOVES 0x80022F80u
#define EVADE_STATES 0x800230ECu
#define BANK_SETUPS 0x80022F8Cu
#define BALL_CLOSER 0x8009F6A0u
#define JOINTS 0x8F4u
#define THROW_LIST 48u

typedef struct ctx {
    const t3_psx_ai_bus_t* bus;
    uint32_t rec,f,o,stale;
    int has_stale;
    uint32_t om,sm;
    int32_t my_first,my_last,my_frame,op_first,op_last,op_frame;
} ctx_t;

/* The AI block is 4-aligned (psx_ai.h): aligned accesses are one load or store. */
typedef uint16_t __attribute__((may_alias)) alias16_t;
typedef uint32_t __attribute__((may_alias)) alias32_t;
static inline uint16_t le16(uint32_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
    return __builtin_bswap16((uint16_t)v);
#else
    return (uint16_t)v;
#endif
}
static inline uint32_t le32(uint32_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__==__ORDER_BIG_ENDIAN__
    return __builtin_bswap32(v);
#else
    return v;
#endif
}
static __attribute__((noinline)) uint32_t rd(const ctx_t* c,uint32_t a,unsigned n) {
    const t3_psx_ai_bus_t* b=c->bus;
    const uint32_t r=a-b->ram_base;
    if(r<b->ram_size && n<=b->ram_size-r) {
        const uint8_t* p=b->ram+r;
        if(n==1u)return p[0];
        if(!(r&(n-1u)))return n==2u?le16(*(const alias16_t*)p):le32(*(const alias32_t*)p);
        return n==2u?(uint32_t)p[0]|(uint32_t)p[1]<<8:
               (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
    }
    if(b->words) {
        const uint32_t w=a&~3u;
        const t3_psx_ai_word_t* e=b->words+((w>>2^w>>9)&b->word_mask);
        if(e->address==w && (a&3u)+n<=4u)
            return *e->host>>(8u*(a&3u))&(n==4u?~0u:(1u<<(8u*n))-1u);
    }
    if(b->memo && a>=b->memo_from) {
        const t3_psx_ai_memo_t* m=b->memo+2u*((a^a>>8^n)&b->memo_mask);
        const uint32_t key=(a&0x1FFFFFFFu)|(uint32_t)n<<29;
        if(m[0].key==key)return m[0].value;
        if(m[1].key==key)return m[1].value;
    }
    const t3_psx_ai_window_t* w=b->windows;
    for(unsigned k=c->bus->window_count;k;--k,++w) {
        const uint32_t o=a-w->base;
        if(o<w->size && n<=w->size-o) {
            const uint8_t* p=w->data+o;
            return n==1u?p[0]:n==2u?(uint32_t)p[0]|(uint32_t)p[1]<<8:
                   (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        }
    }
    return c->bus->read(c->bus->user,a,n);
}
/* A constant guest range as host bytes, or NULL. */
static const uint8_t* flat(const ctx_t* c,uint32_t a,uint32_t n) {
    const t3_psx_ai_window_t* w=c->bus->windows;
    for(unsigned k=c->bus->window_count;k;--k,++w) {
        const uint32_t o=a-w->base;
        if(o<w->size && n<=w->size-o)return w->data+o;
    }
    return 0;
}
static __attribute__((noinline)) void wr(const ctx_t* c,uint32_t a,unsigned n,uint32_t v) {
    const t3_psx_ai_bus_t* b=c->bus;
    const uint32_t r=a-b->ram_base;
    if(r<b->ram_size && n<=b->ram_size-r) {
        uint8_t* p=b->ram+r;
        if(n==1u)p[0]=(uint8_t)v;
        else if(!(r&(n-1u))) {
            if(n==2u)*(alias16_t*)p=le16(v);
            else *(alias32_t*)p=le32(v);
        } else {
            p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);
            if(n>2u){p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
        }
        return;
    }
    if(b->words) {
        const uint32_t w=a&~3u;
        const t3_psx_ai_word_t* e=b->words+((w>>2^w>>9)&b->word_mask);
        if(e->address==w && (a&3u)+n<=4u) {
            const uint32_t m=(n==4u?~0u:(1u<<(8u*n))-1u)<<(8u*(a&3u));
            *e->host=(*e->host&~m)|(v<<(8u*(a&3u))&m);
            return;
        }
    }
    b->write(b->user,a,n,v);
}
#define U8(a) rd(c,(uint32_t)(a),1)
#define U16(a) rd(c,(uint32_t)(a),2)
#define S16(a) ((int32_t)(int16_t)rd(c,(uint32_t)(a),2))
#define U32(a) rd(c,(uint32_t)(a),4)
#define S32(a) ((int32_t)rd(c,(uint32_t)(a),4))
#define P8(a,v) wr(c,(uint32_t)(a),1,(uint32_t)(v))
#define P16(a,v) wr(c,(uint32_t)(a),2,(uint32_t)(v))
#define P32(a,v) wr(c,(uint32_t)(a),4,(uint32_t)(v))
#define REC(off) (c->rec+(off))

static int32_t h16(int32_t v) {return (int16_t)(uint16_t)(uint32_t)v;}
static void probe(ctx_t* c,uint32_t address) {
    if(c->bus->probe)c->bus->probe(c->bus->user,address,c->rec);
}

/* --- branch rows ------------------------------------------------------------------------- */

static int restriction_ok(ctx_t* c,uint32_t f,uint32_t opp,uint32_t value) {
    if(value==0)return 1;
    if(value<0x18)return S16(f+0x16)==(int32_t)value-1;
    if(value<0x2F)return S16(f+0x16)!=(int32_t)value-0x18;
    if(value<0x46)return S16(opp+0x16)==(int32_t)value-0x2F;
    if(value<0x5D)return S16(f+0x18)==(int32_t)value-0x46;
    if(value>0x73)return 0;
    return S16(f+0x18)!=(int32_t)value-0x5D;
}

/* A branch list with 0xC00C common blocks expanded; the list word is re-read
 * at each step, as the generator does. */
typedef struct {uint32_t rows,base;uint32_t k,n;} rows_t;
static void rows_start(rows_t* it,uint32_t rows) {it->rows=rows;it->k=it->n=0;it->base=0;}
static int rows_next(ctx_t* c,rows_t* it,uint32_t* row) {
    for(;;) {
        if(it->k<it->n) {*row=it->base+12u*it->k++;return 1;}
        if(it->n) {it->rows+=12;it->n=0;}
        if(U16(it->rows)==END)return 0;
        if(U16(it->rows)==COMMON) {
            it->base=T3_PSX_AI_COMMON_ROWS+12u*U16(it->rows+6);it->n=U8(it->rows+11);it->k=0;
            if(!it->n)it->rows+=12;
        } else {*row=it->rows;it->rows+=12;return 1;}
    }
}

static uint32_t situation_bit(uint32_t cond) {
    switch(cond) {
    case 0x24:return 1;
    case 0x25:return 2;
    case 0x26:return 4;
    case 0x30:return 8;
    case 0x27:return 0x10;
    default:return 0;
    }
}
static int condition(ctx_t* c,uint32_t row,uint32_t f,uint32_t opp) {
    return c->bus->condition(c->bus->user,row,f,opp)!=0;
}
static int cond_ok(ctx_t* c,uint32_t row,uint32_t f,uint32_t opp,uint32_t sit) {
    const uint32_t k=U8(row+3);
    if(k==0)return 1;
    if(situation_bit(k))return (sit&situation_bit(k))!=0;
    return condition(c,row,f,opp);
}

/* --- random numbers ---------------------------------------------------------------------- */

static uint32_t lcg_next(ctx_t* c) {
    const uint32_t x=U32(T3_PSX_AI_LCG);
    P32(T3_PSX_AI_LCG,5u*x+3u);
    return x;
}
static int32_t rand15(ctx_t* c) {
    const uint32_t x=U32(T3_PSX_AI_RAND)*0x41C64E6Du+0x3039u;
    P32(T3_PSX_AI_RAND,x);
    return (int32_t)(x>>16&0x7FFFu);
}
/* FUN_800569D0: n * ((rand() + LCG) & 0x7FFF) >> 15. */
static int32_t random_below(ctx_t* c,int32_t n) {
    const int32_t r=rand15(c);
    const uint32_t x=lcg_next(c);
    int32_t p=n*(int32_t)(((uint32_t)r+x)&0x7FFFu);
    if(p<0)p+=0x7FFF;
    return p>>15;
}

/* --- candidates -------------------------------------------------------------------------- */

static uint32_t entry(int32_t k) {return T3_PSX_AI_CANDIDATES+12u*(uint32_t)k;}

/* AiCollectCandidates (0x80056A4C), with its two defects (game-bugs.md). */
typedef int (*open_fn)(ctx_t*,uint32_t row,uint32_t f,uint32_t opp);
static int open_now_row(ctx_t* c,uint32_t row,uint32_t f,uint32_t opp) {
    const int32_t frame=S16(f+0x58);
    return (int32_t)U8(row+9)<=frame && frame<=(int32_t)U8(row+10) && restriction_ok(c,f,opp,U8(row+2));
}
static int open_lookahead_row(ctx_t* c,uint32_t row,uint32_t f,uint32_t opp) {
    return U8(row+9)<2 && S16(f+0x58)<=(int32_t)U8(row+10) && restriction_ok(c,f,opp,U8(row+2));
}
static int collect_cond(ctx_t* c,uint32_t k,uint32_t cond_row,int lookahead,uint32_t f,uint32_t opp,uint32_t sit) {
    if(k==0)return 1;
    if(k==0x24 || k==0x25)return (sit&situation_bit(k))!=0;
    if(k==0x26 || k==0x30 || k==0x27)return lookahead?1:(sit&situation_bit(k))!=0;
    return condition(c,cond_row,f,opp);
}
static uint32_t collect_walk(ctx_t* c,uint32_t start,open_fn is_open,int lookahead,int32_t* count,
                             uint32_t index,uint32_t f,uint32_t opp,uint32_t sit) {
    uint32_t r=start,cond_row=start;
    for(;;) {
        const uint32_t head=U16(r);
        if(head==END)break;
        if(head==COMMON) {
            const uint32_t base=T3_PSX_AI_COMMON_ROWS+12u*U16(r+6);
            const uint32_t n_rows=U8(r+11);
            const uint8_t* flat_rows=flat(c,base,12u*n_rows);
            const int32_t frame=S16(f+0x58);
            for(uint32_t k=0;k<n_rows;++k) {
                const uint32_t row=base+12u*k;
                cond_row=row;
                int open;uint32_t kind;
                if(flat_rows) {
                    /* Both predicates test the frame window, then the restriction; the row is constant. */
                    const uint8_t* q=flat_rows+12u*k;
                    if(lookahead?!((int32_t)q[9]<2 && frame<=(int32_t)q[10])
                                :!((int32_t)q[9]<=frame && frame<=(int32_t)q[10]))continue;
                    open=restriction_ok(c,f,opp,q[2]);kind=q[3];
                } else {open=is_open(c,row,f,opp);kind=U8(row+3);}
                if(open && collect_cond(c,kind,row,lookahead,f,opp,sit)) {
                    if(*count>(int32_t)T3_PSX_AI_MAX_CANDIDATES-1)return r;
                    const uint32_t e=entry((*count)++);
                    P32(e,U32(index+4u*U16(row+6)));P32(e+4,row);P16(e+8,0);
                }
            }
            cond_row=base+12u*n_rows;
        } else if(is_open(c,r,f,opp) && collect_cond(c,U8(r+3),lookahead?cond_row:r,lookahead,f,opp,sit)) {
            if(*count>(int32_t)T3_PSX_AI_MAX_CANDIDATES-1)return r;
            const uint32_t e=entry((*count)++);
            P32(e,U32(index+4u*U16(r+6)));P32(e+4,r);P16(e+8,0);
        }
        r+=12;
    }
    return r;
}

/* The same walk on host bytes: rows from the bus windows, entries straight into
 * the AI block, the fighter words it compares read once. 0 when a row lies
 * outside the windows; the caller then repeats the walk on the bus (the
 * entries written so far are the same prefix). */
typedef struct {
    const t3_psx_ai_window_t* span;
    uint32_t f,opp,index,sit;
    int32_t frame,self16,self18,opp16;
    uint8_t dropped[T3_PSX_AI_MAX_CANDIDATES]; /* target +4 bit 23, per entry */
    /* The frames with the same window results, and whether only those and the
     * key decide the walk (no BranchCondition, no candidate limit). */
    int32_t first,last;int reusable;
    uint32_t condition[T3_PSX_AI_COLLECT_CONDITIONS],conditions,held;
} native_t;
/* A window test that also narrows [first,last] to the frames giving its result. */
static inline int native_open(native_t* n,int lookahead,int32_t lo,int32_t hi) {
    if(lookahead && lo>=2)return 0;
    if(!lookahead && n->frame<lo) {if(n->last>lo-1)n->last=lo-1;return 0;}
    if(n->frame>hi) {if(n->first<hi+1)n->first=hi+1;return 0;}
    if(!lookahead && n->first<lo)n->first=lo;
    if(n->last>hi)n->last=hi;
    return 1;
}
static inline int native_cond(ctx_t* c,native_t* n,uint32_t k,uint32_t cond_row,int lookahead) {
    if(k==0)return 1;
    if(k==0x24 || k==0x25)return (n->sit&situation_bit(k))!=0;
    if(k==0x26 || k==0x30 || k==0x27)return lookahead?1:(n->sit&situation_bit(k))!=0;
    const int r=condition(c,cond_row,n->f,n->opp);
    if(n->conditions<T3_PSX_AI_COLLECT_CONDITIONS) {
        n->held|=(uint32_t)(r!=0)<<n->conditions;
        n->condition[n->conditions++]=cond_row;
    } else n->reusable=0;
    return r;
}
static const uint8_t* native_row(const ctx_t* c,native_t* n,uint32_t a,uint32_t bytes) {
    const t3_psx_ai_window_t* w=n->span;
    if(w && a-w->base<w->size && bytes<=w->size-(a-w->base))return w->data+(a-w->base);
    w=c->bus->windows;
    for(unsigned k=c->bus->window_count;k;--k,++w)
        if(a-w->base<w->size && bytes<=w->size-(a-w->base)) {n->span=w;return w->data+(a-w->base);}
    return 0;
}
static int native_restriction(const native_t* n,uint32_t value) {
    if(value==0)return 1;
    if(value<0x18)return n->self16==(int32_t)value-1;
    if(value<0x2F)return n->self16!=(int32_t)value-0x18;
    if(value<0x46)return n->opp16==(int32_t)value-0x2F;
    if(value<0x5D)return n->self18==(int32_t)value-0x46;
    if(value>0x73)return 0;
    return n->self18!=(int32_t)value-0x5D;
}
static void native_put(const ctx_t* c,uint32_t a,unsigned bytes,uint32_t v) {
    uint8_t* p=c->bus->ram+(a-c->bus->ram_base);
    for(unsigned i=0;i<bytes;++i)p[i]=(uint8_t)(v>>(8u*i));
}
static inline __attribute__((always_inline)) int native_add(ctx_t* c,native_t* n,int32_t* count,uint32_t row,const uint8_t* q) {
    const uint32_t k=(uint32_t)(*count)++,e=entry((int32_t)k);
    t3_psx_ai_memo_t* pick=c->bus->picks?c->bus->picks+((row>>2^row>>9)&c->bus->pick_mask):0;
    uint32_t target,dropped;
    if(pick && pick->key==row) {target=pick->value&~1u;dropped=pick->value&1u;}
    else {
        target=U32(n->index+4u*(uint32_t)(q[6]|q[7]<<8));
        dropped=(U32(target+4)&0x800000u)?1u:0u;
        if(pick && target && !(target&1u)) {pick->key=row;pick->value=target|dropped;}
    }
    n->dropped[k]=(uint8_t)dropped;
    uint8_t* p=c->bus->ram+(e-c->bus->ram_base);
    if(!((e-c->bus->ram_base)&3u)) {
        ((alias32_t*)p)[0]=le32(target);((alias32_t*)p)[1]=le32(row);*(alias16_t*)(p+8)=0;
    } else {
        p[0]=(uint8_t)target;p[1]=(uint8_t)(target>>8);p[2]=(uint8_t)(target>>16);p[3]=(uint8_t)(target>>24);
        p[4]=(uint8_t)row;p[5]=(uint8_t)(row>>8);p[6]=(uint8_t)(row>>16);p[7]=(uint8_t)(row>>24);
        p[8]=p[9]=0;
    }
    return 1;
}
static int native_walk(ctx_t* c,native_t* n,uint32_t start,int lookahead,int32_t* count,uint32_t* stop) {
    uint32_t r=start,cond_row=start;
    for(;;) {
        const uint8_t* q=native_row(c,n,r,12);
        if(!q)return 0;
        const uint32_t head=q[0]|(uint32_t)q[1]<<8;
        if(head==END)break;
        if(head==COMMON) {
            const uint32_t base=T3_PSX_AI_COMMON_ROWS+12u*(q[6]|(uint32_t)q[7]<<8),n_rows=q[11];
            const uint8_t* rows=n_rows?native_row(c,n,base,12u*n_rows):q;
            if(!rows)return 0;
            for(uint32_t k=0;k<n_rows;++k) {
                const uint8_t* p=rows+12u*k;
                const uint32_t row=base+12u*k;
                cond_row=row;
                if(!native_open(n,lookahead,p[9],p[10]))continue;
                if(!native_restriction(n,p[2]) || !native_cond(c,n,p[3],row,lookahead))continue;
                if(*count>(int32_t)T3_PSX_AI_MAX_CANDIDATES-1){*stop=r;n->reusable=0;return 1;}
                native_add(c,n,count,row,p);
                n->span=0; /* the index read may have moved the cached window */
            }
            cond_row=base+12u*n_rows;
        } else if(native_open(n,lookahead,q[9],q[10]) &&
                  native_restriction(n,q[2]) && native_cond(c,n,q[3],lookahead?cond_row:r,lookahead)) {
            if(*count>(int32_t)T3_PSX_AI_MAX_CANDIDATES-1){*stop=r;n->reusable=0;return 1;}
            native_add(c,n,count,r,q);
        }
        r+=12;
    }
    *stop=r;
    return 1;
}
/* The walk asks the same BranchConditions in the same order while each gives the same result. */
static int same_conditions(ctx_t* c,const t3_psx_ai_collect_t* m,uint32_t f,uint32_t opp) {
    for(uint32_t k=0;k<m->conditions;++k)
        if((condition(c,m->condition[k],f,opp)!=0)!=(m->held>>k&1u))return 0;
    return 1;
}
static int native_collect(ctx_t* c,int current_only,int32_t* out) {
    const t3_psx_ai_bus_t* b=c->bus;
    if(!b->ram || T3_PSX_AI_CANDIDATES-b->ram_base+12u*T3_PSX_AI_MAX_CANDIDATES>b->ram_size)return 0;
    native_t n; /* dropped[k] is written before it is read */
    n.span=0;n.conditions=n.held=0;
    n.f=U32(T3_PSX_AI_SELF);n.opp=U32(T3_PSX_AI_OPP);
    n.index=U32(REC(0xC));n.sit=U32(REC(0x64));
    n.frame=S16(n.f+0x58);n.self16=S16(n.f+0x16);n.self18=S16(n.f+0x18);n.opp16=S16(n.opp+0x16);
    t3_psx_ai_collect_t* m=b->picks?b->collect:0;
    if(b->picks && *b->pick_index!=n.index) {
        for(uint32_t k=0;k<=b->pick_mask;++k)b->picks[k].key=0;
        *b->pick_index=n.index;
        if(m)m->valid=0;
    }
    const uint32_t start=U32(U32(REC(0x58))+0xC),move=U32(n.f+0x54);
    const uint8_t* ram=b->ram-b->ram_base;
    if(m && m->valid && !current_only && m->f==n.f && m->start==start && m->move==move && m->index==n.index &&
       m->sit==n.sit && m->self16==n.self16 && m->self18==n.self18 && m->opp16==n.opp16 &&
       m->first<=n.frame && n.frame<=m->last && same_conditions(c,m,n.f,n.opp)) {
        /* The same walk: its entries are still there; marks and drops again. */
        for(int32_t k=0;k<m->count;++k) {
            const uint8_t* p=ram+entry(k);
            const uint32_t target=p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
            const uint32_t row=p[4]|(uint32_t)p[5]<<8|(uint32_t)p[6]<<16|(uint32_t)p[7]<<24;
            const t3_psx_ai_memo_t* pick=b->picks+((row>>2^row>>9)&b->pick_mask);
            const int dropped=pick->key==row?(int)(pick->value&1u):(U32(target+4)&0x800000u)!=0;
            native_put(c,entry(k)+8,2,dropped || (c->has_stale && target==c->stale)?0xFFFFu:0u);
        }
        *out=m->count;
        return 1;
    }
    if(m)m->valid=0;
    n.first=INT32_MIN;n.last=INT32_MAX;n.reusable=1;
    int32_t count=0;uint32_t stop;
    if(!native_walk(c,&n,start,0,&count,&stop))return 0;
    if(!current_only) {
        const uint32_t next=U32(n.index+4u*U16(stop+6));
        if(native_open(&n,0,U8(stop+9),U8(stop+10)) && next!=move) {
            uint32_t ignored;
            if(!native_walk(c,&n,U32(next+0xC),1,&count,&ignored))return 0;
        }
        for(int32_t k=0;k<count;++k) {
            const uint32_t e=entry(k);
            const uint8_t* p=ram+e;
            const uint32_t target=p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
            if(n.dropped[k] || (c->has_stale && target==c->stale))
                native_put(c,e+8,2,0xFFFFu);
        }
        if(m && n.reusable) {
            *m=(t3_psx_ai_collect_t){1u,n.f,start,move,n.index,n.sit,n.self16,n.self18,n.opp16,n.first,n.last,count,
                                     {0},n.conditions,n.held};
            for(uint32_t k=0;k<n.conditions;++k)m->condition[k]=n.condition[k];
        }
    }
    *out=count;
    return 1;
}
static void run_filter(ctx_t* c);
static int32_t collect_candidates(ctx_t* c,int current_only) {
    int32_t native;
    if(native_collect(c,current_only,&native)) {
        if(current_only)return native;
        if(U32(REC(0x7C))) {P16(REC(0x70),native);run_filter(c);}
        return native;
    }
    const uint32_t f=U32(T3_PSX_AI_SELF),opp=U32(T3_PSX_AI_OPP);
    const uint32_t index=U32(REC(0xC)),sit=U32(REC(0x64));
    const uint32_t rows=U32(U32(REC(0x58))+0xC);
    int32_t count=0;
    const uint32_t stop=collect_walk(c,rows,open_now_row,0,&count,index,f,opp,sit);
    if(current_only)return count;
    const uint32_t next=U32(index+4u*U16(stop+6));
    const int32_t frame=S16(f+0x58);
    if((int32_t)U8(stop+9)<=frame && frame<=(int32_t)U8(stop+10) && next!=U32(f+0x54))
        collect_walk(c,U32(next+0xC),open_lookahead_row,1,&count,index,f,opp,sit);
    for(int32_t k=0;k<count;++k) {
        const uint32_t e=entry(k),target=U32(e);
        if(U32(target+4)&0x800000u || (c->has_stale && target==c->stale))P16(e+8,-1);
    }
    if(U32(REC(0x7C))) {
        P16(REC(0x70),count);
        run_filter(c);
    }
    return count;
}

/* The loop every filter shares. */
typedef int (*pred_fn)(ctx_t*,uint32_t target,uint32_t branch);
static int32_t mark_candidates(ctx_t* c,pred_fn pred) {
    const int32_t n=S16(REC(0x70));
    int32_t marked=0;
    for(int32_t k=0;k<n;++k) {
        const uint32_t e=entry(k);
        if(S16(e+8)==0 && pred(c,U32(e),U32(e+4))) {P16(e+8,1);++marked;}
    }
    if(n>0)P16(REC(0x72),S16(REC(0x72))+marked);
    return marked;
}

static uint32_t reach_bits(ctx_t* c,uint32_t move) {
    const uint32_t w=U32(move+8);
    return (w&0xFFFF0000u)|(w&0x1C0000u?0:0xC0000u);
}
static int reach_ok(ctx_t* c,uint32_t target) {
    return (reach_bits(c,target)&U32(BAND_REACH+4u*U32(REC(0x14))))!=0;
}
static int is_attack(ctx_t* c,uint32_t t) {
    return U8(t+0x2D)!=0 && U8(t+0x19)==0 && !(U32(t+8)&HINT_NEVER) && !(U32(t+4)&0x20000u);
}
static int grounded_or_special(ctx_t* c,uint32_t t) {
    return U8(t+0x19)==0 || (U32(t+4)&0x80000u) || (U32(t+8)&0x200000u);
}
static int attack_in_reach(ctx_t* c,uint32_t t) {
    return !(U32(t+8)&HINT_NEVER) && !(U32(t+4)&0x20000u) && (reach_ok(c,t) || (U32(t+4)&0x10000u));
}

static int p_attacks(ctx_t* c,uint32_t t,uint32_t br) {
    if(!grounded_or_special(c,t))return 0;
    if(U16(br)==SIDE_STEP || (U32(t+4)&0x80000u))return 1;
    return U8(t+0x2D)!=0 && attack_in_reach(c,t);
}
static int32_t filter_attacks(ctx_t* c) {return mark_candidates(c,p_attacks);}
static int p_attacks_no_special(ctx_t* c,uint32_t t,uint32_t br) {
    const uint32_t a=U32(t+8)&0xFFFFu;(void)br;
    return grounded_or_special(c,t) && U8(t+0x2D)!=0 && a!=0x412 && a!=0x706 && attack_in_reach(c,t);
}
static int32_t filter_attacks_no_special(ctx_t* c) {return mark_candidates(c,p_attacks_no_special);}
static int p_fast(ctx_t* c,uint32_t t,uint32_t br) {
    (void)br;
    return S16(t+0x14)>0 && U8(t+0x2D)!=0 && U8(t+0x19)==0 && U8(t+0x2D)<14 && attack_in_reach(c,t);
}
static int32_t filter_fast_attacks(ctx_t* c) {return mark_candidates(c,p_fast);}
static int p_vs_posture(ctx_t* c,uint32_t t,uint32_t br) {
    (void)br;
    if(!is_attack(c,t) || S16(t+0x14)<0 || !reach_ok(c,t))return 0;
    const uint32_t g=U32(REC(0x230)),a=U32(t+8)&0xFFFFu;
    if(g&2)return a!=0x412;
    if(g&1)return a!=0x412 && a!=0x10F;
    return !(g&4 && (a==0x412 || a==0x217));
}
static int32_t filter_vs_posture(ctx_t* c) {return mark_candidates(c,p_vs_posture);}
static int level_vs_state(uint32_t g,uint32_t a) {
    if((g&0xFFFFu)==0x4C02)return 1;
    if(g&0x400)return a==0x412 || a==0x217 || a==0x31F;
    if(g&4)return a==0x10F || a==0x51F;
    if(g&1)return a==0x217 || a==0x10F;
    if(g&2)return a==0x10F || a==0x217 || a==0x412;
    return 0;
}
static int p_punish(ctx_t* c,uint32_t t,uint32_t br) {
    (void)br;
    return is_attack(c,t) && reach_ok(c,t) && level_vs_state(U32(REC(0x230)),U32(t+8)&0xFFFFu);
}
static int32_t filter_punish_level(ctx_t* c) {return mark_candidates(c,p_punish);}
static int p_punish_no_high(ctx_t* c,uint32_t t,uint32_t br) {
    const uint32_t a=U32(t+8)&0xFFFFu;(void)br;
    return a!=0x412 && is_attack(c,t) && reach_ok(c,t) && level_vs_state(U32(REC(0x230)),a);
}
static int32_t filter_punish_level_no_high(ctx_t* c) {return mark_candidates(c,p_punish_no_high);}
static int p_plain(ctx_t* c,uint32_t t,uint32_t br) {
    return is_attack(c,t) && !(U16(br)&0x3FE0u) && S16(t+0x14)>=0 && reach_ok(c,t);
}
static int32_t filter_plain_attacks(ctx_t* c) {return mark_candidates(c,p_plain);}
static int p_quick(ctx_t* c,uint32_t t,uint32_t br) {
    const uint32_t s=U32(t+4);(void)br;
    return !(U32(t+8)&HINT_NEVER) && S16(t+0x14)>0 && U8(t+0x2D)!=0 && (U8(t+0x19)==0 || (s&0x80000u))
        && (int32_t)U8(t+0x2D)<S16(REC(0x20A)) && !(s&0x20000u) && reach_ok(c,t);
}
static int32_t filter_quick_attacks(ctx_t* c) {return mark_candidates(c,p_quick);}
static int p_quick_no_high(ctx_t* c,uint32_t t,uint32_t br) {
    const uint32_t w=U32(t+8);(void)br;
    return !(w&HINT_NEVER) && S16(t+0x14)>0 && U8(t+0x2D)!=0 && (int32_t)U8(t+0x2D)<S16(REC(0x20A))
        && (w&0xFFFFu)!=0x412 && !(U32(t+4)&0x20000u) && reach_ok(c,t);
}
static int32_t filter_quick_attacks_no_high(ctx_t* c) {return mark_candidates(c,p_quick_no_high);}
static int p_side_step(ctx_t* c,uint32_t t,uint32_t br) {(void)t;return U16(br)==SIDE_STEP;}
static int p_side_step_alt(ctx_t* c,uint32_t t,uint32_t br) {(void)t;return U16(br)==SIDE_STEP_ALT;}

/* FUN_80058AC0. */
static int32_t filter_side_steps_then_quick(ctx_t* c) {
    const uint32_t band=U32(REC(0x14));
    const uint32_t r=lcg_next(c)>>1&0xFFFu;
    int32_t count=0;
    if((band==2 && r<0x20) || (band==3 && r<0x400))count=mark_candidates(c,p_side_step);
    if(U32(REC(0x230))&1)return count+filter_quick_attacks_no_high(c);
    return count+filter_quick_attacks(c);
}

/* FUN_800582A0: the last block's count replaces the earlier one. */
static int p_neutral(ctx_t* c,uint32_t t,uint32_t br) {(void)br;return (U32(t+4)&1)==1 && U8(t+0x2D)==0;}
static int guard_pred(ctx_t* c,uint32_t t,uint32_t br,uint32_t a,uint32_t b,int low_bit_clear) {
    const uint32_t s=U32(t+4);
    if(U16(br)==SIDE_STEP_ALT || (low_bit_clear && (s&0x100)))return 0;
    return (s&0xFFFFu)==a || (s&0xFFFFu)==b;
}
static int p_guard_both(ctx_t* c,uint32_t t,uint32_t br) {return guard_pred(c,t,br,GUARD_HIGH,GUARD_LOW,1);}
static int p_guard_high(ctx_t* c,uint32_t t,uint32_t br) {return guard_pred(c,t,br,GUARD_HIGH,GUARD_HIGH,0);}
static int p_guard_low_state(ctx_t* c,uint32_t t,uint32_t br) {(void)br;return (U32(t+4)&0xFFFFu)==GUARD_LOW;}
static int32_t side_steps_if_closing(ctx_t* c) {
    if(S32(REC(0x18))<3)return 0;
    return mark_candidates(c,p_side_step_alt);
}
static int32_t filter_defence(ctx_t* c) {
    if(S32(REC(0x1C))>0xB86)return 0;
    const uint32_t attack=U32(REC(0x234));
    if(S16(REC(0x214))) {
        const int32_t count=attack!=0x217?mark_candidates(c,p_neutral):0;
        return count+side_steps_if_closing(c);
    }
    int32_t count=0;
    if(S16(REC(0x20C))) {
        if(attack==0x31F)count=mark_candidates(c,p_guard_both);
        else if(attack==0x217)count=mark_candidates(c,p_guard_high);
        else if(attack==0x10F) {
            count=mark_candidates(c,p_guard_low_state);
            count+=side_steps_if_closing(c);
        } else if(attack==0x412) {
            const uint32_t posture=U16(U32(T3_PSX_AI_SELF)+0x60);
            if(posture==0x2021 || posture==GUARD_LOW)count=mark_candidates(c,p_neutral);
            else {
                count=mark_candidates(c,p_guard_high);
                if((lcg_next(c)&0xFFFu)%50u==0)count+=mark_candidates(c,p_neutral);
                count+=side_steps_if_closing(c);
            }
        } else if(attack==0x706) {
            count=mark_candidates(c,p_neutral);
            count+=side_steps_if_closing(c);
        }
    }
    if(S32(REC(0x24))< -0x46 && S32(REC(0x18))<3)count=mark_candidates(c,p_side_step_alt);
    return count;
}

/* FUN_800616B4. */
static int32_t filter_no_lows(ctx_t* c) {
    int32_t removed=0;
    for(int32_t k=0;k<S16(REC(0x70));++k) {
        const uint32_t e=entry(k);
        const int32_t mark=S16(e+8);
        if(mark>=0) {
            const uint32_t word=U32(U32(e)+8);
            if(word&0x10000u || (word&0xFFFFu)==0x10F) {
                if(mark>0)++removed;
                P16(e+8,-1);
            }
        }
    }
    P16(REC(0x72),S16(REC(0x72))-removed);
    return removed;
}
static void run_filter(ctx_t* c) {
    /* FUN_8005967C installs the only filter. */
    if(U32(REC(0x7C))==FILTER_NO_LOWS)filter_no_lows(c);
}

/* FUN_80057300. */
static int hint_never_open(ctx_t* c) {
    const uint32_t f=U32(T3_PSX_AI_SELF),opp=U32(T3_PSX_AI_OPP);
    const uint32_t index=U32(REC(0xC)),sit=U32(REC(0x64));
    const int32_t frame=S16(f+0x58);
    rows_t it;uint32_t row;
    rows_start(&it,U32(U32(f+0x54)+0xC));
    while(rows_next(c,&it,&row))
        if((int32_t)U8(row+9)<=frame && frame<=(int32_t)U8(row+10) && restriction_ok(c,f,opp,U8(row+2))
           && cond_ok(c,row,f,opp,sit) && (U32(U32(index+4u*U16(row+6))+8)&HINT_NEVER))return 1;
    return 0;
}

/* --- pad input --------------------------------------------------------------------------- */

/* AiPadFromStep (0x80061AC4). */
static uint32_t pad_from_step(ctx_t* c,uint32_t step) {
    uint32_t pad=step&0x100?0x80u:0;
    if(step&0x200)pad|=0x10;
    if(step&0x400)pad|=0x40;
    if(step&0x800)pad|=0x20;
    return pad|U16(NUMPAD_PAD+2u*(step&0xF));
}
/* AiStartScript (0x80061B24). */
static void start_script(ctx_t* c,uint32_t script) {
    P32(REC(8),U16(script+2)?script+2:0);
    P16(REC(4),pad_from_step(c,U16(script)));
}
/* FUN_8002CFD0. */
static uint32_t input_sequence(ctx_t* c,uint32_t cmd) {
    if(cmd<0xC00E)return cmd==0xC001?SEQ_C001:cmd==0xC002?SEQ_C002:0;
    if(cmd<0xC7FF)return cmd-0xC00E<0x3F?U32(INPUT_SEQ_A+4u*(cmd-0xC00E)):0;
    return cmd-0xC7FF<0x29?U32(INPUT_SEQ_B+4u*(cmd-0xC7FF)):0;
}
/* AiPressCommand (0x80061BA8). */
static void press_command(ctx_t* c,uint32_t cmd,uint32_t target) {
    cmd&=0xFFFF;
    if(!(cmd&0xC000)) {
        uint32_t d=9,buttons=0;
        while(d>0 && !(U16(DIRECTION_MASKS+2u*d)&(cmd>>5)&0x1FF))--d;
        for(uint32_t k=0;k<4;++k)if(cmd>>k&1)buttons|=U16(BUTTON_PAD+2u*k);
        P16(REC(4),U16(DIRECTION_PAD+2u*d)|buttons);
    } else {
        const uint32_t seq=input_sequence(c,cmd);
        if(seq==0 || (cmd&0xC000)!=0xC000)return;
        const uint32_t step=U16(seq+2);
        P32(REC(8),U16(seq+4)?seq+4:0);
        P16(REC(4),pad_from_step(c,step));
    }
    P32(REC(0x5C),target);
}
/* FUN_80056A30. */
static void reset_approach(ctx_t* c) {
    P16(REC(0x40),0x28);P16(REC(0x3A),0);P16(REC(0x38),0);P16(REC(0x48),-1);
}
/* AiSideStep (0x80061D40); side -1 chooses. */
static void side_step(ctx_t* c,int32_t side) {
    if(side==-1) {
        const uint32_t f=U32(T3_PSX_AI_SELF),opp=U32(T3_PSX_AI_OPP);
        const uint32_t h=U16(f+0x3C);
        side=0;
        if(h-0x101u>0x7EFEu) {
            side=1;
            if(((h+0x7FFFu)&0xFFFFu)>0x7EFDu)side=rand15(c)>>4&1;
        }
        if(S16(opp+0x1278)<S16(f+0x1278))side=1-side;
    }
    const uint32_t script=SIDE_STEP_SCRIPTS+24u*(uint32_t)side;
    const uint32_t first=U16(script);
    P32(REC(8),U16(script+2)?script+2:0);
    P16(REC(4),pad_from_step(c,first));
    reset_approach(c);
}

/* --- executing a candidate --------------------------------------------------------------- */

/* AiAfterAttack (0x80058C40). */
static void after_attack(ctx_t* c,uint32_t branch) {
    const uint32_t self=U32(T3_PSX_AI_SELF);
    const uint32_t index=U32(T3_PSX_AI_INDEX+4u*U8(self+0x1886));
    const uint32_t target=U32(index+4u*U16(branch+6));
    if(U32(target+8)&HINT_NEVER)P16(REC(0x96),S16(REC(0x96))+1);
    if(U8(target+0x2D)==0)return;
    const uint32_t u=lcg_next(c)&3;
    P16(REC(0x68),S16(REC(0x23C))+S16(REC(0x23E)+2u*u));
    if(U32(T3_PSX_AI_GAME_MODE)==8) {
        const uint32_t i=2u-U8(self+0x1E);
        const uint32_t words=U32(REC(0x32C));
        P32(FORCE_TARGETS+4u*i,S16(words));
        if(S32(FORCE_TARGETS+8u+4u*i)<0)P32(FORCE_TARGETS+8u+4u*i,S16(words+2));
    }
    P16(REC(0x44),-1);
    if(S16(REC(0x46))>0)P16(REC(0x46),-1);
    P16(REC(0x3A),0);P16(REC(0x38),0);P16(REC(0x40),0x28);P16(REC(0x48),-1);
}
/* AiExecuteCandidate (0x80058D6C), with its forward-walk bias (game-bugs.md). */
static int32_t execute_candidate(ctx_t* c) {
    const int32_t m=S16(REC(0x72));
    if(m<=0)return -1;
    int32_t k=random_below(c,m),pick;
    const int32_t n=S16(REC(0x70));
    if(U16(REC(2))&1) {
        pick=n-1;
        for(int32_t i=0;i<n;++i)
            if(S16(entry(i)+8)>0 && --k<=0) {pick=i;break;}
        if(n<=0)pick=-1;
    } else {
        int32_t i;
        pick=n;
        for(i=n-1;i>=0;--i)
            if(S16(entry(i)+8)>0 && --k<0) {pick=i;break;}
        if(i<0)pick=n>0?0:n;
    }
    if(pick<0)return -1;
    const uint32_t e=entry(pick);
    press_command(c,U16(U32(e+4)),U32(e));
    after_attack(c,U32(e+4));
    return pick;
}

/* FUN_80058ED0: the continuation is read from the row after the last walked
 * row (game-bugs.md). */
static int open_to(ctx_t* c,uint32_t row,uint32_t slot,int first_frame_one,uint32_t f,uint32_t opp,uint32_t sit) {
    if(U16(row+6)!=slot || (first_frame_one && U8(row+9)!=1))return 0;
    const int32_t frame=S16(f+0x58);
    return (int32_t)U8(row+9)<=frame && frame<=(int32_t)U8(row+10) && restriction_ok(c,f,opp,U8(row+2))
        && cond_ok(c,row,f,opp,sit);
}
static int open_to_walk(ctx_t* c,uint32_t rows,uint32_t slot,int first_frame_one,uint32_t f,uint32_t opp,
                        uint32_t sit,uint32_t* after) {
    *after=rows;
    while(U16(rows)!=END) {
        uint32_t base=rows,n=1;
        if(U16(rows)==COMMON) {base=T3_PSX_AI_COMMON_ROWS+12u*U16(rows+6);n=U8(rows+11);}
        *after=base;
        for(uint32_t k=0;k<n;++k) {
            if(open_to(c,base+12u*k,slot,first_frame_one,f,opp,sit))return 1;
            *after=base+12u*(k+1);
        }
        rows+=12;
    }
    return 0;
}
static int branch_open_to(ctx_t* c,uint32_t slot) {
    const uint32_t f=U32(T3_PSX_AI_SELF),opp=U32(T3_PSX_AI_OPP),sit=U32(REC(0x64));
    uint32_t after,ignored;
    slot&=0xFFFF;
    const uint32_t move=U32(f+0x54);
    if(open_to_walk(c,U32(move+0xC),slot,0,f,opp,sit,&after))return 1;
    const uint32_t index=U32(T3_PSX_AI_INDEX+4u*U8(f+0x1886));
    const uint32_t next=U32(index+4u*U16(after+6));
    const int32_t frame=S16(f+0x58);
    if(next==move || !((int32_t)U8(after+9)<=frame && frame<=(int32_t)U8(after+10)))return 0;
    return open_to_walk(c,U32(next+0xC),slot,1,f,opp,sit,&ignored);
}

/* --- character hooks --------------------------------------------------------------------- */
/* Hooks return a pad word, -1 (nothing), -2 (input already set) or -3. */

static void install_no_lows(ctx_t* c) {P32(REC(0x7C),FILTER_NO_LOWS);}

static int32_t hook_vs_paul(ctx_t* c) {
    const uint32_t opp=U32(T3_PSX_AI_OPP);
    if(S16(opp+0xA0)==0x2C7 && S32(REC(0x18))<3) {
        if(S16(opp+0x58)>15)return 0x8000;
        P16(REC(0x68),-1);
    }
    return -1;
}
static int32_t hook_vs_gon(ctx_t* c) {
    const uint32_t opp=U32(T3_PSX_AI_OPP);
    if(S16(opp+0xA0)!=0x137)return -1;
    if(S32(REC(0x18))<2) {P16(REC(0x68),-1);return -2;}
    return S32(REC(0x18))<4?-1:0x2000;
}
/* Reacts to its own slot 0xDE3, which no bank defines (game-bugs.md #11). */
static int32_t hook_as_hwoarang(ctx_t* c) {
    const uint32_t self=U32(T3_PSX_AI_SELF);
    if(S16(self+0xA0)!=0xDE3)return -1;
    if(U16(self+0x58)-0xDu>4u || S16(REC(0x20A))>9)return (rand15(c)&0xF)==0?-1:0x2000;
    return -1;
}
static int32_t hook_vs_yoshimitsu(ctx_t* c) {
    const uint32_t self=U32(T3_PSX_AI_SELF),opp=U32(T3_PSX_AI_OPP);
    const int32_t frame=S16(opp+0x58),move=S16(opp+0xA0);
    const int32_t band=S32(REC(0x14)),closing=S32(REC(0x18));
    const uint32_t pm=U32(opp+0x54);
    int32_t limit;
    if(move==0x16F) {
        if(closing<3)P16(REC(0x68),-1);
        return -1;
    }
    if(move==0x456) {
        if(closing>2)return 0x2000;
        P16(REC(0x8C),4);install_no_lows(c);P16(REC(0x68),-1);
        return -1;
    }
    if(U16(opp+0xA0)-0x459u>2u) {
        if(move==0x472)return -1;
        if(move!=0x16A && move!=0x43A) {
            if(move!=0x458 && move!=0x49C && move!=0x49D && move!=0x49E)return -1;
            if(S16(self+0x3E)>0x3FFF) {
                if(band<2)P16(REC(0x68),-1);
                return -1;
            }
            if(band<2) {P16(REC(0x68),1);return 0;}
            if(band<3) {install_no_lows(c);P16(REC(0x68),-1);return -1;}
            return 0x2000;
        }
        if(U8(pm+0x2D)==0 || frame<=(int32_t)U8(pm+0x2E)) {
            if(S16(self+0x3E)>0x1000)return closing<2?-1:0x2000;
            if(closing>2 && U32(REC(0x1C))<0xA8Du) {start_script(c,IDLE_SCRIPT);return -2;}
            if(closing<2 && S16(REC(0x20A))>10)return -1;
            if(closing<3 && S16(REC(0x20A))>10) {side_step(c,rand15(c)&1);return -2;}
        } else P16(REC(0x68),-1);
        return 0;
    }
    /* the opponent's slots 0x459-0x45B */
    if(band<1) {
        if((int32_t)U8(pm+0x2D)<=frame && frame<=(int32_t)U8(pm+0x2E))return 0;
        install_no_lows(c);P16(REC(0x68),-1);P16(REC(0x8C),4);
        return -2;
    }
    if(U16(opp+0x30)-0x2000u<0x4001u) {
        P16(REC(0x8C),0x14);
        if(band<2) {install_no_lows(c);P16(REC(0x68),-1);return -1;}
        limit=1;
    } else {
        if(band<3) {
            if(rand15(c)&7)return 0;
            if(U32(REC(0x224))&0x400001u)return 0;
            side_step(c,-1);
            return -2;
        }
        P16(REC(0x8C),10);
        limit=2;
    }
    return band>limit?0x2000:-1;
}
/* Inlined in AiHookVsOgre. */
static int32_t ogre_side(ctx_t* c) {
    const uint32_t self=U32(T3_PSX_AI_SELF),opp=U32(T3_PSX_AI_OPP);
    if(U32(U32(self+0x54)+4)&0x400000u || U32(REC(0x1C))>=0x1B59u)return -1;
    const uint32_t h=U16(self+0x3C);
    int32_t side;
    if(h-0x101u<0x7EFFu)side=0;
    else if(((h+0x7FFFu)&0xFFFFu)<0x7EFEu)side=1;
    else return -1;
    if(S16(opp+0x1278)<S16(self+0x1278))side=1-side;
    side_step(c,side);
    return side;
}
static int32_t ogre_give_up(ctx_t* c) {P16(REC(0x68),-1);return 0x2000;}
static int32_t ogre_step_twice(ctx_t* c,int32_t side) {side_step(c,side);return -2;}
/* The 0xDE4 branch is dead (game-bugs.md #11). */
static int32_t hook_vs_ogre(ctx_t* c) {
    const uint32_t self=U32(T3_PSX_AI_SELF),opp=U32(T3_PSX_AI_OPP);
    const uint32_t pm=U32(opp+0x54);
    const int32_t frame=S16(opp+0x58),band=S32(REC(0x14)),move=S16(opp+0xA0);
    if(move==0xDE4) {
        P16(REC(0x8C),8);
        if((uint32_t)(frame-9)<5u && band<2 && (rand15(c)&0x1F)==0) {install_no_lows(c);return ogre_give_up(c);}
        if(frame<(int32_t)U8(pm+0x2D)-0x1E) {
            if(band<=1)return ogre_give_up(c);
            if(band<3) {P16(REC(0x68),S16(REC(0x68))-4);return 0x2000;}
            const int32_t side=ogre_side(c);
            return side==-1?0x2000:ogre_step_twice(c,side);
        }
        if(frame<(int32_t)U8(pm+0x2D)-0x14) {
            if(S16(self+0x30)>0x37FF) {
                if(band<3)return 0x2000;
                const int32_t side=ogre_side(c);
                return side==-1?0x2000:ogre_step_twice(c,side);
            }
            if(band<3)return 0;
            if(band<4)return 0x2000;
        } else if(frame<(int32_t)U8(pm+0x2E)) {
            if(S16(self+0x30)>0x5FFF) {P16(REC(0x68),8);return 0x8000;}
            if(S16(self+0x30)>0x37FF) {
                if(band>1)return 0x2000;
                P16(REC(0x68),8);
                return 0;
            }
            const int32_t side=ogre_side(c);
            if(side!=-1)return ogre_step_twice(c,side);
            return S16(self+0x32)==0?0x2000:0;
        } else if(frame<(int32_t)U8(pm+0x2E)+0x28) {
            if(band<2)return ogre_give_up(c);
        } else if(band<3)return ogre_give_up(c);
    } else if(move==0x16E) {
        P16(REC(0x8C),8);
        if(frame<(int32_t)U8(pm+0x2D)-8) {
            int32_t side=-1;
            if(((U16(self+0x2E)+0x2000u)&0xFFFFu)<0x4001u && !(U32(U32(self+0x54)+4)&0x400000u)
               && U32(REC(0x1C))<0x1B59u)
                side=S16(self+0x1278)<S16(opp+0x1278);
            if(band<1)return ogre_give_up(c);
            if(band<3)return 0x2000;
            if((rand15(c)&7)==0 && S16(self+0x32)==0 && side!=-1)return ogre_step_twice(c,side);
        } else {
            if((int32_t)U8(pm+0x2E)<=frame)return -1;
            if(S16(self+0x32)==0)return S32(REC(0x1C))<0x1B59?0x6000:0;
            if(band<2)return (int32_t)U16(OGRE_ESCAPES+2u*(uint32_t)(rand15(c)&3));
            if(band<3)return 0x6000;
        }
    } else return -1;
    return (rand15(c)&7)==0?0x2000:0;
}
static int32_t hook_as_king(ctx_t* c) {
    const uint32_t self=U32(T3_PSX_AI_SELF),opp=U32(T3_PSX_AI_OPP);
    const uint32_t u=lcg_next(c)&0xFFF;
    if(S16(REC(0x68))>0 || (int32_t)u>=S16(REC(0x24E)))return -1;
    const int32_t band=S32(REC(0x14));
    const uint32_t own_move=U32(self+0x54);
    uint32_t script;
    if(S16(opp+0x16)!=0x13 && (U32(REC(0x230))&2) && band<3 && U8(opp+0xDB)==0 && U8(opp+0xDF)==0
       && U8(own_move+0x2D)==0 && S16(REC(0x20A))>8 && S16(self+0x32)==0 && S16(self+0x40)==0
       && (rand15(c)&7)==0 && branch_open_to(c,0x15)) {
        start_script(c,KING_THROWS+(uint32_t)((rand15(c)&0xFFF)%3)*14u);
        P16(REC(0xD8),1);
        return -2;
    }
    if((U32(REC(0x230))&1) && band<=1 && U8(opp+0xDF)==0 && U8(own_move+0x2D)==0 && S16(REC(0x20A))>=9
       && S16(self+0x32)==0 && S16(self+0x40)==0 && hint_never_open(c))
        script=KING_VS_CROUCH;
    else {
        if(S16(REC(0x208))==0 || band>1 || (S16(self+0x40)!=0 && S16(self+0x40)!=2) || S16(self+0x32)
           || U8(opp+0xDF) || U8(own_move+0x2D) || S16(REC(0x20A))<9 || S16(REC(0x88)) || !hint_never_open(c))
            return -1;
        script=KING_CLOSE;
    }
    start_script(c,script+(uint32_t)(rand15(c)&1)*14u);
    return -2;
}

static int32_t run_hook(ctx_t* c,uint32_t hook) {
    switch(hook) {
    case 0x80062BACu:return hook_vs_paul(c);
    case 0x80062804u:return hook_as_king(c);
    case 0x80061E70u:return hook_vs_yoshimitsu(c);
    case 0x80062C84u:return hook_as_hwoarang(c);
    case 0x800621D8u:return hook_vs_ogre(c);
    case 0x80062C28u:return hook_vs_gon(c);
    default:return -1; /* 0x80062C7C, 0x80062C10, 0x80062C18, 0x80062C20 */
    }
}
/* AiHookForBank (0x80062B50). */
static uint32_t hook_for_bank(ctx_t* c,int own,int32_t bank) {
    for(uint32_t k=0;k<10;++k) {
        const uint32_t e=HOOK_TABLE+12u*k;
        if(U16(e)==(uint32_t)bank)return U32(own==0?e+8:e+4);
    }
    return 0;
}
/* AiCallHook (0x800594C4). */
static int call_hook(ctx_t* c,int own) {
    if(S16(REC(0x8A))>=0)return 0;
    const uint32_t x=lcg_next(c);
    if(S16(REC(0x272))<=(int32_t)(x&0xFFF))return 0;
    const uint32_t hook=U32(REC(own==0?0x74:0x78));
    if(hook==0)return 0;
    const int32_t r=run_hook(c,hook);
    if(r==-2)return 1;
    if(r==-3)return S16(REC(0x72))>0 && execute_candidate(c)>=0;
    if(r==-1)return 0;
    if(r==0x2000 || r==0x8000) {P16(REC(0x38),0x10);P16(REC(0x3A),0);P16(REC(0x40),0x38);}
    P16(REC(4),(uint32_t)r&0xFFFF);
    return 1;
}

/* --- reset and parameters ---------------------------------------------------------------- */

static void copy_bytes(ctx_t* c,uint32_t dst,uint32_t src,uint32_t n) {
    for(uint32_t i=0;i<n;++i)P8(dst+i,U8(src+i));
}
/* AiLoadParams (0x80061770). */
static void load_params(ctx_t* c,int32_t group,int32_t level,int32_t disabled) {
    if(group<1)group=(int32_t)U16(T3_PSX_AI_DIFFICULTY);
    if(level<1)level=(int32_t)U8(T3_PSX_AI_CPU_LEVEL);
    P16(REC(0x21E),(uint32_t)disabled&0xFFFF);
    if(U32(T3_PSX_AI_TEAM_OR_SURVIVAL))level=4;
    if((uint32_t)group>2)group=2;
    if((uint32_t)level>9)level=9;
    copy_bytes(c,REC(0x238),AI_PARAMS+0x44Cu*(uint32_t)group+0x6Eu*(uint32_t)level,0x6E);
    copy_bytes(c,REC(0x2A6),COMMON_PARAMS,0x6E);
    copy_bytes(c,REC(0x314),BAND_TABLE+12u*(uint32_t)S16(U32(T3_PSX_AI_SELF)+0x18),12);
    copy_bytes(c,REC(0x320),BAND_TABLE+12u*(uint32_t)S16(U32(T3_PSX_AI_OPP)+0x18),12);
    const int32_t a=S16(REC(0x26E)),b=S16(REC(0x26A));
    P16(REC(0x1D4),a>b?a:b);
    if(U32(T3_PSX_AI_GAME_MODE)==8) {
        if(level>3)level=3;
        P32(REC(0x32C),FORCE_WORDS+0x1Eu*((uint32_t)group&3)+6u*(uint32_t)level);
    }
}

/* AiReset field initialisations in code order; size 0 = fighter +0x3F4. */
static const struct {uint16_t off;uint8_t size;int8_t value;} RESET_FIELDS[]={
    {0x34,2,-1},{0x36,2,0},{0x3C,2,0},{0x4A,2,-1},{0x4C,2,0},{0x8A,2,-1},{0x48,2,-1},
    {0x38,2,-1},{0x3A,2,0},{0x58,4,0},{0xA0,4,0},{0x54,2,-1},{0x3E,2,0},{0x46,2,-1},
    {0x44,2,-1},{0x4E,2,-1},{0x2C,2,0},{0x2A,2,0},{0x42,2,-1},{0x60,4,0},{0x8C,2,-1},
    {0x1FE,2,-1},{0x6C,2,-1},{0x6A,2,0},{0x72,2,0},{0x70,2,0},{0xC0,2,0},{0xC2,2,0},
    {0x30,0,0},{0xD4,2,0},{0xD8,2,0},{0xDA,2,0},{0x19C,2,0},{0x1CC,2,0},{0x1A0,2,0},
    {0x1A2,2,0},{0x1CE,2,0},{0x1D2,2,0},{0x96,2,0},{0x98,2,0},{0x9C,2,-1},{0xBC,4,0},
    {0x6E,2,0},{0x08,4,0},{0x216,2,-1},{0x1FC,2,0}};

static void reset_record(ctx_t* c,int32_t group,int32_t level,int32_t disabled) {
    const uint32_t self=U32(T3_PSX_AI_SELF),opp=U32(T3_PSX_AI_OPP);
    for(size_t i=0;i<sizeof RESET_FIELDS/sizeof RESET_FIELDS[0];++i) {
        const uint32_t off=RESET_FIELDS[i].off;
        if(RESET_FIELDS[i].size==0)P32(REC(off),U32(self+0x3F4));
        else if(RESET_FIELDS[i].size==2)P16(REC(off),RESET_FIELDS[i].value);
        else P32(REC(off),RESET_FIELDS[i].value);
    }
    for(uint32_t off=0x1DC;off<0x1FC;off+=4)P32(REC(off),0);
    static const uint16_t bytes[]={0x8F,0x8E,0x91,0x90,0x92,0x93,0x94};
    for(size_t i=0;i<sizeof bytes/sizeof bytes[0];++i)P8(REC(bytes[i]),0);
    load_params(c,group,level,disabled);
    P32(REC(0x74),hook_for_bank(c,1,S16(opp+0x16)));
    P32(REC(0x78),hook_for_bank(c,0,S16(self+0x16)));
    if(U32(T3_PSX_AI_TEAM_OR_SURVIVAL)) {
        for(uint32_t off=0x23E;off<=0x244;off+=2)P16(REC(off),S16(REC(off))/2);
        P16(REC(0x254),S16(REC(0x254))/4);
    }
    P16(REC(0x68),S16(REC(0x23E)));
    const int32_t r=rand15(c);
    const uint32_t u=((uint32_t)r+lcg_next(c))&0x7FFFu;
    P16(REC(0x40),(u*30u>>15)+30u);
    for(uint32_t k=0;k<T3_PSX_AI_MAX_CANDIDATES;++k)P16(entry((int32_t)k)+8,-1);
}

void t3_psx_ai_reset(const t3_psx_ai_bus_t* bus,uint32_t fighter,int group,int level,int disabled) {
    ctx_t context={0},*c=&context;
    c->bus=bus;
    c->rec=T3_PSX_AI_RECORDS+T3_PSX_AI_RECORD_SIZE*U8(fighter+0x1886);
    reset_record(c,group,level,disabled);
}

void t3_psx_ai_init_round(const t3_psx_ai_bus_t* bus) {
    ctx_t context={0},*c=&context;
    c->bus=bus;
    const uint32_t mode=U32(T3_PSX_AI_GAME_MODE);
    static const int8_t normal[2][3]={{0,1,-1},{1,0,-1}};
    static const int8_t force1[2][3]={{-1,0,1},{-1,0,0}};
    static const int8_t force2[2][3]={{0,-1,1},{1,-1,1}};
    const int8_t (*layout)[3]=normal;
    P32(T3_PSX_AI_MULTI,U32(T3_PSX_AI_TEAM_OR_SURVIVAL) || mode==8);
    for(uint32_t k=0;k<4;++k)P32(FORCE_TARGETS+4u*k,-1);
    if(mode==8) {
        const uint32_t which=U32(FORCE_LAYOUT);
        if(which==1)layout=force1;
        else if(which==2)layout=force2;
        else return;
    }
    for(uint32_t i=0;i<3;++i) {
        const uint32_t fighter=T3_PSX_AI_FIGHTER_BASE+T3_PSX_AI_FIGHTER_STRIDE*i;
        const int32_t slot=layout[0][i],target=layout[1][i];
        P8(fighter+0x1886,slot);P8(fighter+0x1887,target);
        if(slot<0)continue;
        c->rec=T3_PSX_AI_RECORDS+T3_PSX_AI_RECORD_SIZE*(uint32_t)slot;
        P16(REC(2),slot);P16(REC(0),slot);
        const uint32_t opp=T3_PSX_AI_FIGHTER_BASE+T3_PSX_AI_FIGHTER_STRIDE*(uint32_t)target;
        P32(T3_PSX_AI_OPP,opp);P32(REC(0x10),opp);P32(T3_PSX_AI_SELF,fighter);
        const uint32_t index=U32(U32(T3_PSX_AI_BANKS+4u*(uint32_t)slot)+0xC);
        P32(REC(0xC),index);P32(T3_PSX_AI_INDEX+4u*(uint32_t)slot,index);
        P16(REC(6),0);P16(REC(4),0);
        reset_record(c,-1,-1,-1);
    }
}

/* --- AiUpdate ---------------------------------------------------------------------------- */

static void dec(ctx_t* c,uint32_t a) {
    const int32_t v=S16(a);
    if(v>=0)P16(a,v-1);
}
static int32_t wrap32(int64_t v) {return (int32_t)(uint32_t)(uint64_t)v;}

static void situation(ctx_t* c) {
    const uint32_t f=c->f,o=c->o;
    P32(REC(0x5C),0);
    const int32_t frame=S16(f+0x58),prev=S16(REC(0x34));
    P16(REC(0x34),frame);
    if(frame!=prev)P16(REC(2),S16(REC(2))+1);
    const uint32_t sm=U32(f+0x54),om=U32(o+0x54);
    P32(REC(0x224),U32(sm+4));P32(REC(0x220),U32(sm+8));P32(REC(0x228),U16(sm+8));
    P32(REC(0x230),U32(om+4));P32(REC(0x22C),U32(om+8));P32(REC(0x234),U16(om+8));
    P32(REC(0x1C),U32(f+0xF8));
    uint32_t sit=S16(f+0x30)>0x4000;
    P16(REC(0x2E),S16(f+0x30));
    const uint32_t heading=U16(f+0x2E);
    if(heading-0x4E38u<0x31C8u)sit|=2;
    if(((heading+0x8000u)&0xFFFFu)<0x31C7u)sit|=4;
    if(S16(o+0x64)==0x412 && S16(o+0x58)<=(int32_t)U8(om+0x2E) && U32(T3_PSX_AI_FIGHTER_DISTANCE)<0x700u)
        sit|=8;
    if(S16(f+0x32)==0)sit|=0x10;
    P8(REC(0x201),0);P8(REC(0x200),0);
    P32(REC(0x64),sit);
    if(S16(o+0x16)==0xE && U8(U32(om+0x28))==0x18)P8(REC(0x200),1);
    else if(S16(o+0x16)==0x13 && U8(U32(om+0x28))==0x1A) {
        P8(REC(0x201),1);
        P32(REC(0x22C),U32(REC(0x22C))|0x10000u);
    }
}

/* Pending script steps, the disabled modes (+0x21E) and the alternate-frame
 * rule; 1 ends the decision. */
static int fixed_input(ctx_t* c) {
    const uint32_t p=U32(REC(8));
    if(p) {
        const uint32_t step=U16(p);
        P32(REC(8),p+2);
        if(S16(p+2)==0)P32(REC(8),0);
        P16(REC(4),pad_from_step(c,step));
        return 1;
    }
    const int32_t mode=S16(REC(0x21E));
    if(mode==0) {P16(REC(4),0);return 1;}
    if(mode==1) {P16(REC(4),U32(REC(0x224))<<10&0x1000u);return 1;}
    const uint32_t slot=U16(REC(0));
    if((U32(T3_PSX_AI_MULTI) && (U32(T3_PSX_AI_FRAME_COUNTER)&slot&1))
       || (U32(T3_PSX_AI_GAME_MODE)==8 && S32(FORCE_TARGETS+8u+4u*slot)>0)) {
        P16(REC(4),U16(REC(6))&0xF000u);
        return 1;
    }
    return 0;
}

static void distance_bands(ctx_t* c) {
    const uint32_t f=c->f,o=c->o;
    const int32_t d=S32(REC(0x1C));
    P32(BALL_CLOSER,0);
    const int32_t prev=S32(REC(0x20));
    P32(REC(0x20),d);
    const int64_t e=(int64_t)d-S16(REC(0x32A))-0x80;
    P32(REC(0x24),wrap32((int64_t)d-prev));
    if(e<S16(REC(0x314))) {
        int pushed=0;
        static const uint16_t offs[]={0x3E8,0x3EC,0x3F0};
        for(unsigned k=0;k<3;++k)pushed|=S32(f+offs[k])!=0 || S32(o+offs[k])!=0;
        if(!pushed) {
            P32(REC(0x14),1);
            if(S16(REC(0x50))==0)P16(REC(0x68),S16(REC(0x68))-1);
        } else {
            P32(REC(0x14),0);
            if(S16(REC(0x50))==0)P16(REC(0x68),S16(REC(0x68))-2);
        }
        P16(REC(0x2A),0);
    } else if(e<S16(REC(0x316))) {
        P32(REC(0x14),2);P16(REC(0x2A),0);
    } else if(e<S16(REC(0x318))) {
        P32(REC(0x14),3);P16(REC(0x2A),0);
    } else {
        const int32_t band=e<S16(REC(0x31A))?4:5,extra=band==4?2:3;
        P32(REC(0x14),band);
        const int32_t r=rand15(c);
        const uint32_t v=(((uint32_t)r+U32(T3_PSX_AI_LCG))&0x7FFFu)*(uint32_t)extra;
        lcg_next(c);
        P16(REC(0x2A),h16(S16(REC(0x2A))+band+(int32_t)(v>>15)));
    }
    if(S16(REC(0x50))==0 || U8(o+0xDB))P16(REC(0x68),S16(REC(0x68))-1);
    int32_t g=wrap32((int64_t)S32(REC(0x1C))-S16(REC(0x31E)));
    if(S32(REC(0x24))<0)g=wrap32((int64_t)g+S32(REC(0x24)));
    static const struct {uint16_t off,margin;} edges[]={{0x320,0x80},{0x322,0x100},{0x324,0x200},{0x326,0x280}};
    int32_t band=1;
    for(unsigned k=0;k<4;++k) {
        if(g<S16(REC(edges[k].off))+edges[k].margin)break;
        ++band;
    }
    P32(REC(0x18),band);
}

static void opponent_attack(ctx_t* c) {
    const uint32_t om=c->om;
    const int32_t first=c->op_first,last=c->op_last,frame=c->op_frame;
    if(first!=0 || !(U32(REC(0x230))&0x80000u))P16(REC(0x20E),0);
    else {
        P16(REC(0x20E),S16(REC(0x20E))+1);
        if((int32_t)U16(om+0x10)==S16(c->o+0xA0))P16(REC(0x20E),0);
    }
    const int32_t coming=S16(REC(0x20E))>0 || (first!=0 && frame<=last+1);
    P16(REC(0x20C),coming);
    if(coming==0 || first-3<=frame)P32(REC(0x80),0);
    else P32(REC(0x80),S32(REC(0x80))+1);
    if(c->my_first==0 || frame<=last+1)P32(REC(0x84),0);
    else P32(REC(0x84),S32(REC(0x84))+1);
    if(S16(REC(0x20C))==0)P16(REC(0x6C),-1);
    else if(S16(REC(0x6C))>0)P16(REC(0x6C),S16(REC(0x6C))-1);
    if(S16(REC(0x20C))==0 && (U32(REC(0x230))&0x200000u))P16(REC(0x20C),1);
    if(U32(REC(0x230))&0x80000u)P16(REC(0x210),S16(REC(0x210))+1);
    else P16(REC(0x210),0);
    const int32_t grab=(U32(REC(0x22C))&0x400000u) && (first==0 || frame<=last);
    P16(REC(0x214),grab);
    if(S16(REC(0x20C))==0 && grab==0) {
        P16(REC(0x20A),999);P8(REC(0x92),0);
    } else if(first==0) {
        P16(REC(0x20A),(int32_t)U8(U32(om+0xC)+10)-frame+1);
    } else {
        int32_t value=0x3E5;
        if(frame<=last) {
            P16(REC(0x20A),h16(first-frame));
            value=h16(first-frame)<0x65?-1:0x3E6;
        }
        if(value>=0) {P16(REC(0x214),0);P16(REC(0x20C),0);P16(REC(0x20A),value);}
    }
}

static int needs_guard(ctx_t* c) {
    const uint32_t posture=U16(REC(0x224));
    if(S32(REC(0x1C))>=0xB87 || S16(REC(0x88)))return 0;
    const uint32_t attack=U32(REC(0x234));
    if(S16(REC(0x214)))return attack==0x217?0:posture==0x2021 || posture==GUARD_LOW;
    if(S16(REC(0x20C))==0 || S16(REC(0x2E))>0x3000)return 0;
    switch(attack) {
    case 0x412:return posture==0x2021 || posture==GUARD_LOW || posture==GUARD_HIGH;
    case 0x217:return posture==GUARD_HIGH;
    case 0x10F:return posture==GUARD_LOW;
    case 0x31F:return posture==GUARD_LOW || posture==GUARD_HIGH;
    case 0x706:return posture==GUARD_LOW || posture==0x2021;
    default:return 0;
    }
}

static void guard_state(ctx_t* c) {
    const uint32_t f=c->f,o=c->o;
    P16(REC(0x21A),S32(o+JOINTS+0xA0)< -0xC1B);
    if(S16(f+0x3E)>0x3000 || (!(U32(REC(0x230))&0x200000u) && U32(REC(0x234))!=0x607 && U32(REC(0x234))!=0x706))
        P8(REC(0x94),0);
    if(S16(REC(0x54))>=0 && S32(REC(0x18))<2 && S16(REC(0x27E))<=(int32_t)(lcg_next(c)&0xFFF))P8(REC(0x92),1);
    int32_t v=0;
    if((U32(REC(0x230))&0x204u) && (U32(REC(0x230))&0xFFFFu)!=0x4C02)v=U8(o+0xDB)==0;
    P16(REC(0x208),v);
    v=S16(o+0x16)==4 && S16(o+0xA0)==0x456;
    P16(REC(0x88),v);
    if(v)P16(REC(0x216),-1);
    P16(REC(0x6A),needs_guard(c));
    if(!(U32(REC(0x224))&1))P16(REC(0x4A),-1);
    else if(S16(REC(0x3C))>0)P16(REC(0x4A),S16(REC(0x4A))-1);
    else if(!(U32(c->sm+0x24)>>8&0x10)) {
        const uint32_t x0=lcg_next(c),x1=lcg_next(c),x2=lcg_next(c);
        P16(REC(0x4A),(x0&7)+(x1&0xF)+0x14+(x2&7));
    }
    P16(REC(0x50),c->my_first!=0);
    v=0;
    if((U32(c->sm+4)&0x200000u) && (U32(c->sm+8)&0x200000u))v=c->my_frame<=c->my_last;
    P16(REC(0x52),v);
}

/* Per-frame bookkeeping before the decision. */
static void track(ctx_t* c) {
    const uint32_t f=c->f,o=c->o;
    c->om=U32(o+0x54);c->sm=U32(f+0x54);
    c->my_first=U8(c->sm+0x2D);c->my_last=U8(c->sm+0x2E);c->my_frame=S16(f+0x58);
    c->op_first=U8(c->om+0x2D);c->op_last=U8(c->om+0x2E);c->op_frame=S16(o+0x58);
    P32(REC(0x7C),0);
    if(!(U32(c->sm+0x24)>>8&0x20))P16(REC(0xD4),0);
    const uint32_t b8e=U8(REC(0x8E)),b90=U8(REC(0x90));
    P8(REC(0x8E),0);P8(REC(0x90),0);P8(REC(0x8F),b8e);P8(REC(0x91),b90);
    if(S32(o+0x3F4)<=S32(o+0x3F8)/10) {
        static const uint16_t offs[]={0x250,0x252,0x258,0x25A};
        for(unsigned k=0;k<4;++k)P16(REC(offs[k]),0);
    }
    const uint32_t x=lcg_next(c);
    if(S16(REC(0x27E))<=(int32_t)(x&0xFFF) && S16(REC(0x2E))>0x1FFF && S16(f+0x3E)<0x6001 && S32(REC(0x18))<3)
        P8(REC(0x8F),1);
    P16(REC(0x28),c->sm!=U32(REC(0x58)) || c->my_frame<S16(REC(0x4E)));
    P16(REC(0x204),c->om!=U32(REC(0x1D8)) || c->op_frame<S16(REC(0x206)));
    P16(REC(0x2C),S16(REC(0x2C))+1);
    if(U32(REC(0x58))!=c->sm) {P32(REC(0x58),c->sm);P16(REC(0x2C),0);}
    P16(REC(0x4E),c->my_frame);
    if(U32(REC(0x224))&0x400000u)reset_approach(c);
    if(c->my_first==0) {
        if(!(U32(REC(0x220))&0x80000u) && U8(o+0x87)==0)P16(REC(0x1D2),0);
    } else if(c->my_first==c->my_frame)P16(REC(0x1D2),S16(REC(0x1D2))+1);
    if(c->op_first==0) {
        if(!(U32(REC(0x22C))&0x80000u) && U8(f+0x87)==0)P16(REC(0x216),-1);
    } else if(c->op_first==c->op_frame) {
        P16(REC(0x216),S16(REC(0x216))+1);
        if(S16(REC(0x216))>10)P16(REC(0x216),10);
    }
    distance_bands(c);
    opponent_attack(c);
    guard_state(c);
}

static int forced_escape(int32_t slot,int32_t bank) {
    return slot==0x189 || slot==0x18E || slot==0x17F || slot==0x181 || (slot==0x172 && (bank==9 || bank==0))
        || (bank==3 && slot==0x152);
}

/* Up to six branch rows of the running throw the CPU may continue with. */
static uint32_t throw_follow_ups(ctx_t* c,uint32_t rows,uint32_t* found) {
    const uint32_t f=c->f,o=c->o;
    const int32_t bank=S16(f+0x16);
    uint32_t n=0;
    while(U16(rows)!=END) {
        uint32_t base=rows,count=1;
        if(U16(rows)==COMMON) {base=T3_PSX_AI_COMMON_ROWS+12u*U16(rows+6);count=U8(rows+11);}
        for(uint32_t k=0;k<count;++k) {
            const uint32_t row=base+12u*k;
            if(!restriction_ok(c,f,o,U8(row+2)))continue;
            const uint32_t u=lcg_next(c)&0x70;
            const int32_t slot=S16(row+6);
            int ok=1;
            if(slot==0x3A7)ok=0;
            else if(slot==0xD41)ok=bank!=4 || u!=0;
            else if(slot==0xD24 || slot==0xD32)ok=bank==0 || bank==0xD?u==0:1;
            else if(slot==0xDE2)ok=!(bank==0 && u!=0);
            else if(slot==0xD25 || slot==0xD34)ok=bank!=0 && bank!=0xD;
            if(ok && (U8(row+3)==0 || condition(c,row,f,o))) {
                found[n++]=row;
                if(n>5)return n;
            }
        }
        rows+=12;
    }
    return n;
}

/* Throw escapes (the CPU is thrown) and throw follow-ups (it throws). */
static int throws(ctx_t* c) {
    const uint32_t f=c->f,o=c->o;
    const int32_t state=S16(f+0x74);
    if(state==0) {
        P32(REC(0xA0),0);P16(REC(0x96),0);P16(REC(0x9C),-1);P32(REC(0xBC),0);
        return 0;
    }
    reset_approach(c);
    const uint32_t move=U32(f+0x54);
    if(state<1) {
        if(move==U32(REC(0xA0))) {P16(REC(4),0);return 1;}
        P16(REC(0x96),S16(REC(0x96))-1);
        P32(REC(0xA0),move);
        if(!forced_escape(S16(o+0xA0),S16(o+0x16)) && (int32_t)(lcg_next(c)&0xFFF)>=S16(REC(0x24A)))return 1;
        rows_t it;uint32_t row,rows[THROW_LIST],n=0;
        rows_start(&it,U32(move+0xC));
        while(n<THROW_LIST && rows_next(c,&it,&row))rows[n++]=row;
        for(uint32_t k=0;k<n;++k)P32(T3_PSX_AI_THROW_ROWS+4u*k,rows[k]);
        int32_t press=0;
        if(n) {
            row=rows[random_below(c,(int32_t)n)];
            const uint32_t k=U8(row+3);
            press=k==0x3A || k==0x3C?1:k==0x3B?2:S16(row);
        }
        if(press)press_command(c,(uint32_t)press,0);
        else P16(REC(4),0);
        return 1;
    }
    if(move!=U32(REC(0xA0)) && S16(REC(0x96))>=0 && S16(REC(0x96))<S16(REC(0x246))-1
       && (int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x24C))) {
        uint32_t found[6];
        const uint32_t n=throw_follow_ups(c,U32(move+0xC),found);
        if(!n) {P16(REC(0x96),-1);P16(REC(0x9C),-1);P32(REC(0xBC),0);}
        else {
            P16(REC(0x96),S16(REC(0x96))+1);
            P32(REC(0xA0),U32(f+0x54));
            const uint32_t row=found[random_below(c,(int32_t)n)];
            P16(REC(0x9C),U8(row+9)+1);
            P32(REC(0xBC),row);
        }
    }
    if(S16(f+0x58)!=S16(REC(0x9C))) {P16(REC(4),0);return 1;}
    const uint32_t row=U32(REC(0xBC));
    const uint32_t index=U32(T3_PSX_AI_INDEX+4u*U8(f+0x1886));
    press_command(c,U16(row),U32(index+4u*U16(row+6)));
    return 1;
}

static int32_t collect(ctx_t* c,int current_only) {
    P16(REC(0x72),0);
    P32(REC(0x58),U32(c->f+0x54));
    const int32_t n=collect_candidates(c,current_only);
    P16(REC(0x70),n);
    return h16(n);
}

static int p_stance_row(ctx_t* c,uint32_t t,uint32_t br) {(void)t;return U8(br+8)-0x1Cu<2u;}
/* Stance moves: execute a 0x1C/0x1D row with probability word 49 (+0x29A). */
static int stance(ctx_t* c) {
    if(S16(REC(0x224))!=0x4C02 || U8(U32(c->f+0x54)+0x2D))return 0;
    reset_approach(c);
    if(collect(c,1)>0 && (int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x29A))) {
        if(mark_candidates(c,p_stance_row)>0) {execute_candidate(c);return 1;}
    }
    P16(REC(4),0);
    return 1;
}

static int32_t action_mask(ctx_t* c) {
    const int32_t band=S32(REC(0x14)),closing=S32(REC(0x18));
    const int32_t first=c->op_first,last=c->op_last,frame=c->op_frame;
    if(first==0) {
        if(band>2) {P16(REC(0x36),0);return closing>=4?7:0x3F;}
        if(!(U16(REC(2))&1))return 0;
        P16(REC(0x36),0);
        if(S16(REC(0x68))<0 && (lcg_next(c)&0x3F)==0)return 0x38;
        return 7;
    }
    if(S16(REC(0x20A))<0xC && frame<=last) {
        if(U32(REC(0x234))==0x10F || (U32(REC(0x22C))&0x10000u)) {
            if(!(U16(REC(2))&1))return 0;
            P16(REC(0x36),0);
            const uint32_t x=U32(T3_PSX_AI_LCG),x1=5u*x+3u;
            if(x&2) {P32(T3_PSX_AI_LCG,x1);return 0x38;}
            if(U8(c->om+0x19)==0 && S16(REC(0x2E))<=0x4000 && !(U32(REC(0x230))&0x200000u)) {
                P32(T3_PSX_AI_LCG,x1);
                return 4;
            }
            P32(T3_PSX_AI_LCG,5u*x1+3u);
            return (x1&0x3F)==0?4:2;
        }
        int32_t mask;
        if(closing<3 && (closing<2 || S32(REC(0x24))<0xB))mask=lcg_next(c)&0xF?0x3F:0;
        else mask=1;
        P16(REC(0x36),0);
        return mask;
    }
    if(band<2) {
        P16(REC(0x36),0);
        return U32(REC(0x230))&2?0x38:0x28;
    }
    return 0;
}

static int p_low_or_hint(ctx_t* c,uint32_t t,uint32_t br) {
    (void)br;
    return (U32(t+8)&0x10000u) || (U32(t+8)&0xFFFFu)==0x10F;
}
static int p_side_step_or_special(ctx_t* c,uint32_t t,uint32_t br) {
    return S16(br)==-0x3FFF || ((U32(t+4)&0xA0000u)==0x80000u && !(U32(t+4)&0x600000u));
}
/* Grounded CPU in a neutral state: crouch-cancel rows or a random action. */
static int crouch_and_actions(ctx_t* c) {
    const uint32_t f=c->f;
    if(U8(f+0xDB) || !(U32(REC(0x224))&0x404u)) {P16(REC(0x36),0);return 0;}
    rows_t it;uint32_t row;int crouch=0;
    rows_start(&it,U32(U32(f+0x54)+0xC));
    while(!crouch && rows_next(c,&it,&row))crouch=U8(row+3)==0x31;
    if(crouch) {
        const uint32_t v=(uint32_t)(S16(REC(0x6E))-1)&0xFFFFu;
        P16(REC(0x36),1);P16(REC(0x6E),v);
        if(h16((int32_t)v)>=0) {P16(REC(4),0);return 1;}
        const uint32_t wait=U8(REC(0x238)+(U32(T3_PSX_AI_LCG)&3));
        lcg_next(c);
        P16(REC(4),0x10);P16(REC(0x6E),wait);
        return 1;
    }
    if(collect(c,1)<1) {P16(REC(4),0);return 1;}
    P16(REC(0x36),1);P16(REC(0x40),0x28);P16(REC(0x48),-1);P16(REC(0x3A),0);P16(REC(0x38),0);
    P16(REC(0x8A),0x10);
    if(U8(REC(0x201)) && (U16(f+0x60)&0x200)) {P16(REC(4),0x8000);return 1;}
    const int32_t first=c->op_first,last=c->op_last,frame=c->op_frame;
    if((((U32(REC(0x230))&0x200000u) && first==0)
        || (U32(REC(0x234))==0x607 && !(U32(REC(0x22C))&0x10000u) && S16(REC(0x20A))<0x1D)) && frame<last) {
        P16(REC(4),0);
        return 1;
    }
    const int32_t mask=action_mask(c);
    if(mask==0) {P16(REC(4),0);return 1;}
    uint32_t action;
    for(;;) {
        const int32_t r=rand15(c);
        const uint32_t u=((uint32_t)r+lcg_next(c))&0x7FFFu;
        action=U16(ACTION_TABLE+2u*(u*6u>>15));
        if((uint32_t)mask&action)break;
    }
    if(action==1) {P16(REC(4),0x1000);return 1;}
    if(action==2) {P16(REC(4),U16(REC(2))&7?0x80:0x4080);return 1;}
    if(action==4) {P16(REC(4),S16(f+0x16)==0x12?0x2000:0x8000);return 1;}
    if(action==8)filter_attacks(c);
    else if(action==0x10)mark_candidates(c,p_low_or_hint);
    else if(action==0x20)mark_candidates(c,p_side_step_or_special);
    if(S16(REC(0x72))>0)execute_candidate(c);
    return 1;
}

static uint32_t self_target(ctx_t* c,uint32_t row) {
    const uint32_t index=U32(T3_PSX_AI_INDEX+4u*U8(c->f+0x1886));
    return U32(index+4u*U16(row+6));
}
static int open_for(ctx_t* c,uint32_t row) {return restriction_ok(c,c->f,c->o,U8(row+2));}

/* Rows of a branch list that pass `pre`, the restriction and `accept`, up to
 * `limit`; `pre` first is the order of _gather_ordered. */
typedef int (*row_fn)(ctx_t*,uint32_t row,void* arg);
static uint32_t gather(ctx_t* c,uint32_t rows,uint32_t limit,row_fn pre,row_fn accept,void* arg,uint32_t* found) {
    uint32_t n=0;
    while(U16(rows)!=END) {
        uint32_t base=rows,count=1;
        if(U16(rows)==COMMON) {base=T3_PSX_AI_COMMON_ROWS+12u*U16(rows+6);count=U8(rows+11);}
        for(uint32_t k=0;k<count;++k) {
            const uint32_t row=base+12u*k;
            if((!pre || pre(c,row,arg)) && open_for(c,row) && accept(c,row,arg)) {
                found[n++]=row;
                if(n>=limit)return n;
            }
        }
        rows+=12;
    }
    return n;
}
static void store_rows(ctx_t* c,uint32_t address,const uint32_t* rows,uint32_t n) {
    for(uint32_t k=0;k<n;++k)P32(address+4u*k,rows[k]);
}
static int r_string(ctx_t* c,uint32_t row,void* arg) {(void)arg;return (U32(self_target(c,row)+0x24)>>8&0x20)!=0;}
static int r_frame_one(ctx_t* c,uint32_t row,void* arg) {
    (void)arg;
    return U8(row+9)==1 && U8(row+10)==U8(row+11);
}
static int r_attack_rows(ctx_t* c,uint32_t row,void* arg) {
    const int32_t word35=*(const int32_t*)arg;
    if((U32(self_target(c,row)+0x24)>>8&0x20) && (int32_t)(lcg_next(c)&0xFFF)<word35)return 0;
    return 1;
}
typedef struct {uint32_t index,reach;int32_t w208,w24e;} planned_t;
static int r_planned(ctx_t* c,uint32_t row,void* arg) {
    const planned_t* p=arg;
    const uint32_t t=U32(p->index+4u*U16(row+6));
    if(!((p->w208==0 || !(U32(t+8)&HINT_NEVER)) && !(U32(t+4)&0x20000u)))return 0;
    uint32_t w=U32(t+8);
    if(!(w&0x200000u) && !(U32(t+4)&0x80000u)) {
        if(!(w&HINT_NEVER))return 0;
        if((int32_t)(lcg_next(c)&0x7FFF)<=p->w24e)return 0;
        w=U32(t+8);
    }
    const uint32_t r=(w&0xFFFF0000u)|(w&0x1C0000u?0:0xC0000u);
    return (r&p->reach)!=0;
}
static uint32_t planned_follow_ups(ctx_t* c,uint32_t rows,uint32_t* found) {
    planned_t p;
    p.index=U32(REC(0xC));p.w208=S16(REC(0x208));p.w24e=S16(REC(0x24E));
    lcg_next(c);
    p.reach=U32(BAND_REACH+4u*U32(REC(0x14)));
    return gather(c,rows,48,NULL,r_planned,&p,found);
}

/* When the CPU's move changed (+0x28), prepare the follow-up lists; otherwise
 * collect candidates, and after 220 frames in one move execute any. */
static int new_move(ctx_t* c) {
    probe(c,0x8005B994);
    const uint32_t f=c->f;
    uint32_t found[48],n;
    int done=0;
    P16(REC(0x72),0);P16(REC(0x70),0);
    if(S16(REC(0x28))) {
        P32(REC(0x58),U32(f+0x54));
        P16(REC(0x4E),S16(f+0x58));
        const uint32_t rows=U32(U32(REC(0x58))+0xC);
        if(S16(REC(0xC0))>=1) {
            n=gather(c,rows,4,NULL,r_string,NULL,found);
            store_rows(c,REC(0xC4),found,n);
            P16(REC(0xC2),n);
            done=1;
            if(n)P16(REC(4),U16(REC(6))&0xF000u);
            else {P16(REC(0xC2),0);P16(REC(0xC0),0);}
        } else if(S16(REC(0xD8))>=1) {
            n=planned_follow_ups(c,rows,found);
            store_rows(c,REC(0xDC),found,n);
            P16(REC(0xDA),n);
            done=1;
            if(n)P16(REC(4),U16(REC(6))&0xF000u);
            else {P16(REC(0xDA),0);P16(REC(0xD8),0);}
        } else if(S16(REC(0x52))==0) {
            if(U8(U32(f+0x54)+0x2D)==0) {
                static const uint16_t offs[]={0x1A2,0xDA,0xC2,0x1A0,0xD8,0xC0,0x98};
                for(unsigned k=0;k<7;++k)P16(REC(offs[k]),0);
            } else {
                n=0;
                if(S16(REC(0x1CC))==0) {
                    P16(REC(0x1D0),0);
                    int32_t word35=S16(REC(0x27E));
                    n=gather(c,rows,10,r_frame_one,r_attack_rows,&word35,found);
                    for(uint32_t k=0;k<n;++k) {
                        P32(REC(0x1A4)+4u*k,found[k]);
                        const int32_t last=U8(found[k]+10);
                        if(last<0x3C && S16(REC(0x1D0))<last)P16(REC(0x1D0),last);
                    }
                    P16(REC(0x1A2),n);
                } else P16(REC(0x1CC),0);
                done=1;
                if(!n) {P16(REC(0x1A2),0);P16(REC(0x1A0),0);}
                else {
                    P16(REC(4),U16(REC(6))&0xF000u);
                    P16(REC(0x1A0),S16(REC(0x1A0))+1);
                }
            }
        }
    }
    P16(REC(0x4E),S16(f+0x58));
    if(!done) {
        const int32_t count=h16(collect_candidates(c,0));
        P16(REC(0x70),count);
        if(S16(REC(0x2C))>0xDC) {
            int32_t marked=0;
            for(int32_t k=0;k<count;++k) {
                const uint32_t e=entry(k)+8;
                if(S16(e)==0) {P16(e,1);++marked;}
            }
            P16(REC(0x72),S16(REC(0x72))+marked);
            if(marked>0) {execute_candidate(c);done=1;}
        }
    }
    /* Tekken Ball's ball_behind (mode 7) is not ported. */
    return done;
}

static int p_all(ctx_t* c,uint32_t t,uint32_t br) {(void)c;(void)t;(void)br;return 1;}
/* During a move usable in the air or as a special (+0x52). */
static int follow_through(ctx_t* c) {
    const uint32_t f=c->f;
    if(!(S16(REC(0x52)) && S16(REC(0x70)) && U8(REC(0x200))==0))return 0;
    if(S16(REC(0x20A))<(int32_t)U8(U32(f+0x54)+0x2D)-S16(f+0x58) || S32(REC(0x14))>2 || S16(f+0x32)==2) {
        if(mark_candidates(c,p_all)>0 && execute_candidate(c)>=0)P16(REC(0x68),-1);
    }
    return 1;
}

static uint32_t duck_or_back(ctx_t* c) {
    if(S16(c->f+0x32)!=0)return 0;
    P16(REC(0x40),0x28);P16(REC(0x48),-1);P16(REC(0x3A),0);P16(REC(0x38),0);
    return U32(REC(0x234))==0x217?0x8000u:0xC000u;
}

/* The opponent's attack is coming: attack first, crouch or duck, or side-step. */
static int counter(ctx_t* c) {
    if(!(S16(REC(0x20C)) && S16(REC(0x216))>=0 && S32(REC(0x18))<4))return 0;
    if(U8(REC(0x8F)) || (int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x25E))) {
        const int32_t v=S16(REC(0x20A));
        if(1<v && v<0x1E && filter_quick_attacks(c)>0 && execute_candidate(c)>=0)return 1;
    }
    if(S16(REC(0x50))==0) {
        const int32_t attack=S16(REC(0x22C));
        if(((attack!=0 && attack!=0x607) || S16(REC(0x20E))>0)
           && (U8(REC(0x92)) || (int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x262)))) {
            P16(REC(4),duck_or_back(c));
            return 1;
        }
    }
    if((int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x25C))) {
        int ok=0;
        if(U16(REC(0x20A))-9u<3u && S16(REC(0x2E))<0x4000)ok=!(U32(REC(0x224))&0x400000u);
        if(ok && S32(REC(0x18))>1) {
            side_step(c,-1);
            P16(REC(0x68),S16(REC(0x68))-10);
            return 1;
        }
    }
    return 0;
}

static int opp_reach_ok(ctx_t* c,uint32_t band_off) {
    return (reach_bits(c,c->om)&U32(BAND_REACH+4u*(uint32_t)S32(REC(band_off))))!=0;
}
static int can_counter(ctx_t* c) {
    return S16(REC(0x20C))!=0 && S16(REC(0x6A))==0 && S16(c->f+0x3E)<0x5000 && S32(REC(0x1C))<0xB87
        && opp_reach_ok(c,0x18);
}
static int draw(ctx_t* c,uint32_t off) {return (int32_t)(lcg_next(c)&0xFFF)<S16(REC(off));}
static int32_t low_parry_or_escape(ctx_t* c) {
    if(U32(REC(0x234))!=0x412)return 1;
    return (lcg_next(c)&0x3F)==0?9:1;
}

static int32_t reaction_body(ctx_t* c,int32_t kind) {
    const uint32_t f=c->f,o=c->o,om=c->om;
    if((U8(om+0x2D) && (int32_t)U8(om+0x2E)<S16(o+0x58)) || S16(f+0x40)==2)P8(REC(0x94),2);
    if(U8(REC(0x94))==0 && (U32(REC(0x234))==0x607 || U32(REC(0x234))==0x706 || (U32(REC(0x230))&0x200000u))
       && draw(c,0x272))
        P8(REC(0x94),1);
    if(S16(REC(0x8C))<0 && U8(REC(0x94))!=2) {
        kind=3;
        if(U8(REC(0x94))!=0)return 3;
    }
    const int32_t bank=S16(f+0x16);
    const int grounded=U32(REC(0x228))==0 && U8(f+0xDB)==0;
    const uint32_t until=U16(REC(0x20A));
    if(until-8u<2u && grounded && S16(GUARD_MOVES+16u*(uint32_t)bank)!=0 && !(U32(om+0x24)>>8&8)
       && U8(o+0xDB)==0 && can_counter(c)) {
        kind=5;
        if(draw(c,0x292))return 5;
    }
    if(bank==9 && 0x15<S16(REC(0x20A)) && S16(REC(0x20A))<0x1B && grounded && can_counter(c)) {
        kind=6;
        if(draw(c,0x292))return 6;
    }
    if(until-4u<7u && grounded && U8(o+0xDB)==0 && can_counter(c) && draw(c,0x292)) {
        const uint32_t attack=U32(REC(0x234));
        if(attack==0x412 || attack==0x217 || attack==0x607 || attack==0x706) {
            kind=7;
            if(S32(GUARD_MOVES+4u+16u*(uint32_t)bank))return 7;
        }
        if(attack==0x10F) {
            kind=8;
            if(S32(GUARD_MOVES+8u+16u*(uint32_t)bank))return 8;
        }
    }
    if(U32(REC(0x22C))&0x400000u) {
        if(forced_escape(S16(o+0xA0),S16(o+0x16))) {
            kind=1;
            if(draw(c,0x272))return 1;
        }
        if(S16(REC(0x20A))<=0x1D && draw(c,0x296)) {P8(REC(0x92),2);return 2;}
    }
    const int32_t attack=S16(REC(0x22C));
    if((attack!=0 && attack!=0x607) || S16(REC(0x20E))>0) {
        if(S16(REC(0x6A))!=0 || S16(REC(0x50))!=0)return 0;
        if(U8(REC(0x92)))return 2;
        const uint32_t x=lcg_next(c);
        if((int32_t)(x&0xFFF)<S16(REC(0x254)) || (S16(REC(0x20E))>0x1E && draw(c,0x256))) {
            P8(REC(0x92),5);
            return 2;
        }
    }
    (void)kind;
    if(S16(REC(0x6C))>8 && (lcg_next(c)&0x1F)==0)return 1;
    if(S16(REC(0x202))!=0 && (lcg_next(c)&0xF)==0)return 1;
    if(U32(o+0x54)==U32(REC(0x1DC)) && S16(REC(0x88))==0 && draw(c,0x25A))return 1;
    if(S16(REC(0x88))==0 && draw(c,0x258))return low_parry_or_escape(c);
    if(S16(REC(0x20E))>=0x1F && draw(c,0x25A))return low_parry_or_escape(c);
    int ok=0;
    if(U16(REC(0x20A))-9u<3u && S16(REC(0x2E))<0x4000)ok=!(U32(REC(0x224))&0x400000u);
    if(!ok || S32(REC(0x18))!=2)return 0;
    if((U32(REC(0x224))&1) || U8(f+0xDB) || S16(REC(0x50))!=0 || U32(REC(0x234))!=0x412)return 0;
    return draw(c,0x25C)?4:0;
}
/* The reaction to the opponent's attack (jump table 0x8002317C). */
static int32_t reaction_kind(ctx_t* c) {
    if(S16(REC(0x216))>=0)return 0;
    int32_t kind=0;
    if(S16(REC(0x88))!=0)return 0;
    if(S16(REC(0x6C))==0) {
        kind=2;
        if(S16(REC(0x50))==0)return 2;
    }
    if(U8(REC(0x94))==0) {
        kind=0;
        if(!(S16(REC(0x20C))!=0 && S32(REC(0x18))<4))return 0;
    }
    return reaction_body(c,kind);
}

/* LAB_8005D7A4. */
static int execute_marked(ctx_t* c,int32_t count,int execute) {
    if(!execute)return 0;
    if(count>0 && execute_candidate(c)>=0) {
        P16(REC(0x40),0x28);P16(REC(0x3A),0);P16(REC(0x38),0);P16(REC(0x48),-1);
    }
    return 1;
}
static uint32_t pad_back_on_even(ctx_t* c) {
    reset_approach(c);
    return U16(REC(2))&1?0:0x2000u;
}

/* The evasion state machine in +0x94 (1 pick, 3 retreat, 4 back off, 5 side
 * step, 6 keep out). */
static int evade(ctx_t* c) {
    const uint32_t f=c->f,o=c->o;
    if(S16(REC(0x21A)))return 0;
    int32_t dist=S32(REC(0x1C));
    const int32_t band=S32(REC(0x14));
    if(U8(REC(0x94))==1) {
        if(band<3 && (int32_t)(lcg_next(c)&0xFFF)>=S16(REC(0x27E)))P8(REC(0x94),3);
        else P8(REC(0x94),U8(EVADE_STATES+2u*(uint32_t)random_below(c,7)));
    }
    if(S16(o+0x16)==4)dist-=0x12C;
    const uint32_t state=U8(REC(0x94));
    if(state!=1 && state!=2 && dist>=0xB87) {P8(REC(0x94),0);P16(REC(4),0);return 1;}
    const int32_t until=S16(REC(0x20A)),facing=S16(f+0x3E);
    if(state==3) {
        reset_approach(c);
        const uint32_t om=U32(o+0x54);
        if(U8(om+0x2D) && (int32_t)U8(om+0x2E)<S16(o+0x58))return 0;
        if(until>=0x1D) {
            if(band>=3) {
                P16(REC(4),pad_back_on_even(c));
                P16(REC(0x68),lcg_next(c)&0xF);
                return 1;
            }
            return filter_attacks_no_special(c)<=0;
        }
        if(until<9) {P16(REC(4),facing<0x4000?0x8000:0);return 1;}
        if(band<3) {
            if(filter_attacks_no_special(c)<=0)return 1;
            P16(REC(0x68),-1);
            return 0;
        }
        if(facing<0x4000) {P8(REC(0x94),4);P16(REC(4),0x8000);return 1;}
        return 0;
    }
    if(state==4) {
        reset_approach(c);
        if(facing>0x4000 && S32(REC(0x18))>=2) {P16(REC(4),0x2000);return 1;}
        if(until>=9 && band<2) {
            if(filter_attacks_no_special(c)<=0)return 1;
            P16(REC(0x68),-1);
            return 0;
        }
        start_script(c,IDLE_SCRIPT);
        return 1;
    }
    if(state==5) {
        if((U32(REC(0x224))&0x400000u) || (lcg_next(c)&7) || until<7) {P16(REC(4),0x2000);return 0;}
        if(facing>=0x4000)P8(REC(0x94),3);
        side_step(c,-1);
        return 1;
    }
    if(state==6) { /* no code path sets state 6 (game-bugs.md) */
        reset_approach(c);
        if(until>=0x1D) {P16(REC(4),S32(REC(0x18))>0?0x8000:0);return 1;}
        if(S32(REC(0x18))>=2 || until<8) {start_script(c,IDLE_SCRIPT);return 1;}
        P16(REC(4),0x3000);P16(REC(0x48),0x14);
        return 1;
    }
    return 0;
}

static int p_quick_neutral_412(ctx_t* c,uint32_t t,uint32_t br) {
    (void)br;
    return U8(t+0x2D)!=0 && (U32(t+4)&0x20001u)==1 && reach_ok(c,t);
}
static int p_slot_counter(ctx_t* c,uint32_t t,uint32_t br) {
    (void)t;
    return U16(br+6)==0x1EE || U16(br+6)==0x6A3;
}
static int p_slot_22c(ctx_t* c,uint32_t t,uint32_t br) {(void)t;return U16(br+6)==0x22C;}

/* The reaction cases; 1 ends the decision. */
static int react(ctx_t* c,int32_t kind) {
    const uint32_t f=c->f;
    int32_t n;
    switch(kind) {
    case 0:return 0;
    case 9:
        n=mark_candidates(c,p_neutral);
        if(n>0) {P8(REC(0x8E),1);P16(REC(0x68),-1);}
        return execute_marked(c,n,n>0);
    case 1: {
        const uint32_t attack=U32(REC(0x234));
        if(S16(REC(0x20C))==0)n=filter_fast_attacks(c);
        else if(attack==0x607 || attack==0x706 || (U32(REC(0x230))&0x200001u))n=filter_quick_attacks_no_high(c);
        else n=filter_quick_attacks(c);
        if(attack==0x412)n+=mark_candidates(c,p_quick_neutral_412);
        return execute_marked(c,n,n>0);
    }
    case 2: {
        n=filter_defence(c);
        if(n>0)return execute_marked(c,n,1);
        uint32_t pad=0;
        if(S16(f+0x32)==0) {
            pad=U32(REC(0x234))==0x217?0x8000u:0xC000u;
            reset_approach(c);
        }
        P16(REC(4),pad);
        return 1;
    }
    case 3:return evade(c);
    case 4:
        side_step(c,-1);
        P16(REC(0x68),S16(REC(0x68))-10);
        return 1;
    case 5:
        n=mark_candidates(c,p_slot_counter);
        return execute_marked(c,n,n>0);
    case 6:
        n=mark_candidates(c,p_slot_22c);
        return execute_marked(c,n,n>0);
    default:
        start_script(c,U32(GUARD_MOVES+16u*(uint32_t)S16(f+0x16)+(kind==7?4u:8u)));
        return 1;
    }
}

static int open_now(ctx_t* c,uint32_t row) {
    const int32_t frame=S16(c->f+0x58);
    return (int32_t)U8(row+9)<=frame && frame<=(int32_t)U8(row+10) && restriction_ok(c,c->f,c->o,U8(row+2))
        && cond_ok(c,row,c->f,c->o,U32(REC(0x64)));
}
/* The prepared follow-up lists are filtered to their open rows and dropped:
 * none is ever executed (game-bugs.md #10). */
static void expire_lists(ctx_t* c) {
    if(S16(REC(0xC0))>0) {
        uint32_t k=0;
        for(int32_t i=0;i<S16(REC(0xC2));++i) {
            const uint32_t row=U32(REC(0xC4)+4u*(uint32_t)i);
            if(open_now(c,row))P32(T3_PSX_AI_THROW_ROWS+4u*k++,row);
        }
        P16(REC(0xC0),0);P16(REC(0xC2),0);P16(REC(0xD4),1);
    }
    if(S16(REC(0xD8))>0) {
        uint32_t k=0;
        for(int32_t i=0;i<S16(REC(0xDA));++i) {
            const uint32_t row=U32(REC(0xDC)+4u*(uint32_t)i);
            if(open_now(c,row))P32(T3_PSX_AI_THROW_ROWS+4u*k++,row);
        }
        P16(REC(0xD8),0);P16(REC(0xDA),0);
    }
    if(S16(REC(0x1A0))>=0) {
        for(int32_t i=0;i<S16(REC(0x1A2));++i) {
            const uint32_t row=U32(REC(0x1A4)+4u*(uint32_t)i);
            if(open_now(c,row))P32(T3_PSX_AI_THROW_ROWS,row);
        }
        P16(REC(0x1A0),0);P16(REC(0x1A2),0);
    }
}

/* Outside its own attack the CPU backs off from airborne attacks at close
 * range, turns towards the opponent, or dashes back. */
static int avoid_air_attack(ctx_t* c) {
    const uint32_t f=c->f,o=c->o;
    if(S16(REC(0x50))!=0)return 0;
    const uint32_t om=U32(o+0x54);
    const int32_t angle=S16(REC(0x2E)),closing=S32(REC(0x18));
    const int a=U8(om+0x19)==0 || closing>1 || S16(REC(0x280))<=(int32_t)(lcg_next(c)&0xFFF);
    if(!a) {
        uint32_t pad=0;
        int b;
        if(angle>0x3FFF || S32(REC(0x24))> -0xB) {
            if((int32_t)U8(om+0x1A)-S16(o+0x58)<0x15 || angle<0x4001)b=1;
            else {b=0;pad=0x2000;}
        } else {b=0;pad=0x6000;}
        if(!b) {P16(REC(4),pad);return 1;}
    }
    if(angle<0x6001 || closing>1) {
        if(angle<=0x1000)return 0;
        if((int32_t)(lcg_next(c)&0x7FFF)>=angle)return 0;
        if((int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x27E)))return 0;
        P16(REC(0x38),8);P16(REC(0x3A),0);P16(REC(0x40),0x30);
        if(S16(f+0x16)==7) {P16(REC(4),angle>0x3FFF?0x8000:0x2000);return 1;}
        uint32_t pad;
        if(S16(REC(0x20C))==0 || (lcg_next(c)&7)) {
            pad=0x2000;
            if(closing<3)pad=U16(REC(2))&1?0x8000u:0x2000u;
        } else pad=duck_or_back(c);
        P16(REC(4),pad);
        return 1;
    }
    if(U8(o+0xDB))P16(REC(4),0);
    else if(S16(REC(0x208)))P16(REC(4),U16(REC(2))&1?0x4040:0x4020);
    else {
        if((int32_t)(lcg_next(c)&0xFFF)>=S16(REC(0x27E))) {P16(REC(0x68),-2);return 0;}
        if(S16(f+0x16)==7)P16(REC(4),angle>0x3FFF?0x8000:0x2000);
        else P16(REC(4),0x2000);
    }
    return 1;
}

static int p_desperation(ctx_t* c,uint32_t t,uint32_t br) {(void)br;return (U32(t+0x24)>>8&0x100)!=0;}
/* LAB_80060350. */
static int32_t last_resort(ctx_t* c,int32_t n) {
    const uint32_t f=c->f;
    if(n==0 && S32(REC(0x18))>2 && S16(f+0x122)==0 && S32(f+0x3F4)<=S32(f+0x3F8)/10
       && (lcg_next(c)&3)==0 && (int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x258)))
        n=mark_candidates(c,p_desperation);
    return n;
}

static int p_ground_tumble(ctx_t* c,uint32_t t,uint32_t br) {(void)br;return (U32(t+4)&0x20000u)!=0;}
static int p_first_row_35(ctx_t* c,uint32_t t,uint32_t br) {
    (void)br;
    return U8(U32(t+0xC)+3)==0x35 && !(U32(t+8)&HINT_NEVER);
}
static int low_or_hint(ctx_t* c,uint32_t t,uint32_t extra) {
    const uint32_t w=U32(t+8);
    return U8(t+0x2D)<0x51 && ((w&0x10000u) || (w&0xFFFFu)==0x51F || (extra && (w&0xFFFFu)==extra))
        && !(w&HINT_NEVER);
}
static int p_low_51f(ctx_t* c,uint32_t t,uint32_t br) {(void)br;return low_or_hint(c,t,0);}
static int p_low_51f_10f(ctx_t* c,uint32_t t,uint32_t br) {(void)br;return low_or_hint(c,t,0x10F);}
static int p_crouch_dash(ctx_t* c,uint32_t t,uint32_t br) {(void)t;return S16(br)==-0x3FFE;}
static int p_side_step_row(ctx_t* c,uint32_t t,uint32_t br) {(void)t;return S16(br)==-0x3FFF;}

/* The opponent is down or crouching (+0x208). */
static int32_t vs_grounded(ctx_t* c,int32_t wait) {
    if(S16(REC(0x50))!=0)return 0;
    uint32_t x=lcg_next(c);
    if(S16(REC(0x27E))<=(int32_t)(x&0xFFF))wait=-1;
    if(!(S16(REC(0x2E))<0x4001 || (lcg_next(c)&0xF)!=0))return mark_candidates(c,p_ground_tumble);
    if(wait>=0)return 0;
    x=lcg_next(c);
    const uint32_t u=x&0xFFF;
    if(u<0x334)return mark_candidates(c,p_first_row_35);
    if(u<0x3AF) {side_step(c,(int32_t)(x&1));return -1;}
    if(u<0x6E2)return mark_candidates(c,p_low_51f);
    if(u<0x75D && !(U32(REC(0x224))&1))return mark_candidates(c,p_neutral);
    if(u<0x829)return mark_candidates(c,p_crouch_dash);
    if((uint32_t)(S32(REC(0x14))-2)<2u && u<0x9C3) {start_script(c,WAKE_UP_SCRIPT);return -1;}
    return mark_candidates(c,p_low_51f_10f);
}

static int p_hint_ground(ctx_t* c,uint32_t t,uint32_t br) {
    (void)br;
    return (U32(t+8)&0x10000u) && U8(t+0x2D)==0 && U8(t+0x19)==0;
}
/* The opponent is in a stance (state 0x4C02). */
static int32_t vs_stance(ctx_t* c) {
    const int32_t band=S32(REC(0x14));
    int32_t n=0;
    if(band<3) {
        if(band>1) {
            if((lcg_next(c)&0xF)==0)n=mark_candidates(c,p_side_step_row);
            else n=mark_candidates(c,p_hint_ground);
        }
    } else {
        int side=1;
        if(S32(REC(0x24))<0x33 && (lcg_next(c)&7)==0) {
            n=mark_candidates(c,p_hint_ground);
            side=0;
        }
        if(side) {
            n=mark_candidates(c,p_side_step_row);
            if(n>0) {P16(REC(0x3A),2);P16(REC(0x38),0xFA);P16(REC(0x40),0x136);}
        }
    }
    if(band<4 && S16(REC(0x21A))==0 && S16(REC(0x27E))<=(int32_t)(lcg_next(c)&0xFFF)) {
        P16(REC(0x68),-1);
        if(U8(c->o+0xDB)==0 && (lcg_next(c)&0x3F)<0x20 && U32(REC(0x7C))==0)n+=filter_punish_level_no_high(c);
        else n+=filter_punish_level(c);
    }
    return n;
}

static int p_throw_rows(ctx_t* c,uint32_t t,uint32_t br) {
    return (U32(t+8)&HINT_NEVER) && (U16(br)&0xC200u)!=0x200u;
}
static int p_plain_throws(ctx_t* c,uint32_t t,uint32_t br) {
    return (U32(t+8)&HINT_NEVER) && (U16(br)&0xFFE0u)==0;
}
static int p_neutral_counter(ctx_t* c,uint32_t t,uint32_t br) {
    (void)br;
    return (U32(t+4)&0xA0000u)==0x80000u && U8(t+0x19)==0;
}
static int p_air_special(ctx_t* c,uint32_t t,uint32_t br) {
    (void)br;
    return U8(t+0x19)!=0 && !(U32(t+4)&0x20000u) && ((U32(t+4)&0x80000u) || (U32(t+8)&0x200000u));
}
static int p_neutral_not_counter(ctx_t* c,uint32_t t,uint32_t br) {
    return U8(t+0x2D)==0 && U8(t+0x19)==0 && !(U32(t+4)&0x20000u) && S16(br+6)!=0x1EE && S16(br+6)!=0x6A3;
}

/* Waiting is over and nothing urgent: throws, bank setups, neutral moves,
 * side steps or the regular attack filters. */
static int32_t approach_attack(ctx_t* c,int32_t threshold) {
    const uint32_t f=c->f,o=c->o;
    const int32_t band=S32(REC(0x14));
    if((U8(REC(0x91)) || (int32_t)(lcg_next(c)&0xFFF)<threshold) && band<2 && S16(o+0x16)!=0x13) {
        const uint32_t x=lcg_next(c);
        int32_t n;
        if((int32_t)(x&0xFFF)<S16(REC(0x24E))) {
            n=mark_candidates(c,p_throw_rows);
            if(n<1)n=mark_candidates(c,p_plain_throws);
            else P16(REC(0x98),1);
        } else n=mark_candidates(c,p_plain_throws);
        if(n>0)return n;
    }
    const uint32_t table=U32(BANK_SETUPS+16u*(uint32_t)S16(f+0x16));
    if(S16(REC(0x42))<0 && table && S16(REC(0x50))==0 && !(U32(REC(0x224))&1) && S16(f+0xA0)!=S16(table+2)
       && S16(REC(0x2E))<0x2001 && U8(f+0xDB)==0 && band<3 && S16(REC(0x208))==0) {
        const int32_t u=lcg_next(c)&0xFFF;
        uint32_t p=table;
        for(;;) {
            if(u<=S16(p)) {
                if(branch_open_to(c,(uint32_t)S16(p+2))) {
                    start_script(c,U32(p+4)+2);
                    P16(REC(0xD8),1);
                    P16(REC(0x42),(lcg_next(c)&0xF)+0x1C);
                    return -1;
                }
                break;
            }
            p+=8;
            if(S16(p)<0)break;
        }
    }
    if(band<3 && U8(f+0xDB)==0 && !(U32(REC(0x224))&1) && (lcg_next(c)&0x3F)<0xE)
        return mark_candidates(c,p_neutral);
    const uint32_t x=lcg_next(c);
    if((x&0x3F)<0x20) {
        if(lcg_next(c)&1)return mark_candidates(c,p_side_step_or_special);
        return filter_punish_level(c);
    }
    int32_t n;
    if(S16(REC(0x44))<0 && S16(REC(0x46))<0 && !(U32(REC(0x224))&1) && U8(f+0xDB)==0
       && (lcg_next(c)&0xFFF)<0x200) {
        n=mark_candidates(c,p_neutral);
        if(n>0) {P16(REC(0x44),0x12);P16(REC(0x68),-1);return n;}
    }
    n=mark_candidates(c,p_side_step_or_special);
    if((int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x250)))return n+filter_vs_posture(c);
    if((int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x252)))return n+filter_fast_attacks(c);
    if((U32(REC(0x230))&1) && (int32_t)(lcg_next(c)&0xFFF)>=S16(REC(0x27E)))return n+filter_attacks_no_special(c);
    if((lcg_next(c)&0x3F)<10)return n+filter_plain_attacks(c);
    if(U8(f+0xDB)==0 && S16(REC(0x46))<0 && random_below(c,0x1000)<0x200) {
        n=mark_candidates(c,p_air_special);
        if(n>0) {
            P16(REC(0x46),4);
            const int32_t a=random_below(c,4),b=random_below(c,4),d=random_below(c,4);
            P16(REC(0x46),a+b+d+1);
            return n;
        }
    }
    if(random_below(c,0x1000)<10)return n+mark_candidates(c,p_neutral_not_counter);
    return n+filter_attacks(c);
}

static int p_air_attack_special(ctx_t* c,uint32_t t,uint32_t br) {
    (void)br;
    return U8(t+0x2D)!=0 && ((U32(t+4)&0x80000u) || (U32(t+8)&0x200000u)) && !(U32(t+4)&0x20000u);
}
/* LAB_8005E7C0: the count of marked candidates (-1: input already set), or
 * *direct for the rare neutral-move pick executed at once. */
static int32_t choose(ctx_t* c,int* direct) {
    probe(c,0x8005E7C0);
    const uint32_t o=c->o;
    int32_t wait=S16(REC(0x68)),threshold=S16(REC(0x248));
    *direct=0;
    if(S16(REC(0xD4))) {
        int32_t removed=0;
        for(int32_t k=0;k<S16(REC(0x70));++k) {
            const uint32_t e=entry(k);
            const int32_t mark=S16(e+8);
            if(mark>=0 && (U32(U32(e)+0x24)>>8&0x20)) {
                if(mark>0)++removed;
                P16(e+8,-1);
            }
        }
        P16(REC(0x72),S16(REC(0x72))-removed);
    }
    if(S32(REC(0x80))>=0x1A && S16(REC(0x6C))<0 && S16(REC(0x6A))==0) {
        const int32_t t=S16(REC(0x20A))-(int32_t)((U32(T3_PSX_AI_LCG)&7)+4);
        lcg_next(c);
        P16(REC(0x6C),t>3?t:3);
    }
    if(S16(REC(0x6C))>=1)return 0;
    const int32_t word35=S16(REC(0x27E));
    if(U8(REC(0x8F)) || U8(REC(0x91)) || (S32(REC(0x80))>0x19 && word35/2<=(int32_t)(lcg_next(c)&0xFFF))
       || ((uint32_t)(S32(REC(0x84))-1)<0x13u && word35<=(int32_t)(lcg_next(c)&0xFFF)))
        wait=-1;
    const uint32_t om=U32(o+0x54);
    const int32_t band=S32(REC(0x14));
    if((int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x280)) && U8(o+0xDB) && S16(REC(0x230))!=0x4C02) {
        const int32_t last=U8(om+0x1A),first=U8(om+0x19),frame=S16(o+0x58);
        if(first+(last-first)/2<=frame && frame<=last && S16(REC(0x2E))<0x4001 && band==2)P16(REC(0x68),-1);
    }
    if(S16(o+0xA0)==6 && band<3 && (int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x258)))P16(REC(0x68),S16(REC(0x68))-2);
    if((U32(REC(0x230))&0x605u) || U8(o+0xDB) || U8(o+0xDF) || S16(o+0x16)==0x13 || S16(REC(0x20A))<10) {
        threshold=0;
        P8(REC(0x91),0);
    }
    /* Tekken Ball's ball_ahead (mode 7) is not ported. */
    if(S16(REC(0x208)) && S16(REC(0x88))==0)return vs_grounded(c,wait);
    if(S16(REC(0x230))==0x4C02 && (int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x274)))return vs_stance(c);
    if(S16(REC(0x46))>=0) {
        const int32_t n=mark_candidates(c,p_air_attack_special);
        if(n>0)P16(REC(0x46),-1);
        return n;
    }
    if(S16(REC(0x20E))!=0) {
        if(U8(REC(0x8F))==0 && S16(REC(0x258))<=(int32_t)(lcg_next(c)&0xFFF)) {
            if(wait<1)return U32(REC(0x230))&1?filter_attacks_no_special(c):filter_attacks(c);
            return 0;
        }
        return filter_side_steps_then_quick(c);
    }
    if((S16(REC(0x20C)) || S16(REC(0x214))) && S16(REC(0x88))==0) {
        if(U8(REC(0x8F)) || (int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x258)))return filter_side_steps_then_quick(c);
        if(wait<1)return U32(REC(0x230))&1?filter_attacks_no_special(c):filter_attacks(c);
        return 0;
    }
    if(wait<1) {
        if(S16(REC(0x44))<0)return approach_attack(c,threshold);
        const int32_t n=mark_candidates(c,p_neutral_counter);
        P16(REC(0x44),-1);
        return n;
    }
    if(band>2 || (U32(REC(0x224))&1) || (lcg_next(c)&0xFFF)>3)return 0;
    if(mark_candidates(c,p_neutral)<1)return 0;
    *direct=1;
    return 0;
}

/* LAB_8005E568..LAB_800605EC: pick and execute an attack; 1 ends the decision. */
static int attack(ctx_t* c) {
    const uint32_t f=c->f;
    if(U32(T3_PSX_AI_GAME_MODE)==8) {
        if(S32(FORCE_TARGETS+4u*U16(REC(0)))>=1)return 0;
        /* FUN_800B2E60 (Tekken Force overlay) returns the player record. */
        if(U8(T3_PSX_AI_FIGHTER_BASE+0x1F)!=U8(f+0x1E)
           && (int32_t)(lcg_next(c)&0xFFF)<S16(U32(REC(0x32C))+4))return 0;
    }
    if(S16(REC(0x4C))>=1) {
        P16(REC(0x4C),S16(REC(0x4C))-1);
        P16(REC(4),U16(REC(6))&0xF000u);
        return 1;
    }
    if(S16(REC(0x44))>=5) {P16(REC(4),U16(REC(6))&0xF000u);return 1;}
    const int32_t band=S32(REC(0x14));
    if(!(band<4 && S16(REC(0x21A))==0 && S16(REC(0x1D2))<S16(REC(0x1D4))))return 0;
    if(band>2 && random_below(c,12)!=0)return 0;
    if(band>1 && random_below(c,8)==0)return 0;
    if(S16(REC(0x3A))!=0) {
        if(S16(REC(0x6C))<1 && (S16(REC(0x20C))==0 || S32(REC(0x18))>3)) {
            if((U32(REC(0x230))&4) && random_below(c,0x1000)<0x100)return 0;
        } else {P16(REC(4),0x8000);return 1;}
    }
    int direct;
    int32_t n=choose(c,&direct);
    if(!direct) {
        n=last_resort(c,n);
        if(n<0) {reset_approach(c);return 1;}
    }
    const int32_t pick=execute_candidate(c);
    if(pick<0)return 0;
    P16(REC(0x40),0x28);P16(REC(0x48),-1);P16(REC(0x3A),0);P16(REC(0x38),0);
    const uint32_t target=U32(entry(pick));
    if(S16(T3_PSX_AI_TEAM_OR_SURVIVAL)==0 && (U32(target+0x24)>>8&0x20))P16(REC(0xC0),1);
    else if(((U32(REC(0x224))&0x80000u) && (lcg_next(c)&0x3F)<0x20)
            || ((U32(target+4)&0x80000u) && (lcg_next(c)&0x3F)<0x30))
        P16(REC(0xD8),1);
    const uint32_t pressed=U32(REC(0x5C));
    const int32_t state=pressed?S16(pressed+4):0;
    if(state==0x2021 || state==0x2829)P16(REC(0x4C),0xC);
    if(U8(target+0x2D)==0 && !(U32(target+4)&0x80000u))return 1;
    P32(REC(0x60),target);
    return 1;
}

static int32_t by_distance(ctx_t* c,int32_t v) {
    switch(S32(REC(0x14))) {
    case 0:case 1:return v<0x14?3:0;
    case 2:
        if(v<0x7A)return 5;
        if(v<0x1EB)return 1;
        if(v<0x214)return 3;
        return v<S16(REC(0x25C))*4096/1000?8:0;
    case 3:return v<0x666?2:v<0xB33?1:0;
    case 4:return v<0xCC?2:v<0x599?4:1;
    case 5:
        if(S16(REC(0x2A))<0x2D1 && S16(REC(0x208))==0 && v>0x11D)return 1;
        return 4;
    default:return 0;
    }
}
static int32_t by_closing(ctx_t* c,int32_t v) {
    switch(S32(REC(0x18))) {
    case 0:case 1:return v<0x199?6:v<0x333?3:0;
    case 2:
        if(v<=0x27)return 2;
        return v<0x170?3:0;
    case 3:
        if(v<0x1EB)return 2;
        if(v<0x214)return 5;
        if(v<0x218)return 3;
        if(v<0x22D)return 1;
        return v<0x241 && (U32(REC(0x224))&2)?8:0;
    case 4:
        if(v<0x80)return 2;
        if(v<0x90)return 5;
        if(v<0xC0)return 8;
        return v<0x3C0?1:0;
    case 5:
        if(S16(REC(0x2A))<0x2D1 && S16(REC(0x208))==0 && v>0x50)return v<0xCC?2:v<0x400?1:v<0x5EB?5:0;
        return 4;
    default:return 0;
    }
}
static int p_crouch_dash_row(ctx_t* c,uint32_t t,uint32_t br) {(void)t;return S16(br)==-0x3FFE;}
/* Movement cases: 1 side step or walk in, 2 walk in, 3 step back, 4 side
 * step or dash in, 5 dash in, 6 crouch dash or step back, 8 side step. */
static int do_movement(ctx_t* c,int32_t choice) {
    if(choice==0)return 0;
    if(choice==1) {
        if(mark_candidates(c,p_side_step_row)>=1) {
            P16(REC(0x3A),0);P16(REC(0x38),0);P16(REC(0x40),0x28);
            return execute_candidate(c)>=0;
        }
        choice=2;
    }
    if(choice==4) {
        if(mark_candidates(c,p_side_step_row)>=1) {
            P16(REC(0x3A),2);P16(REC(0x38),0xFA);P16(REC(0x40),0x136);
            return execute_candidate(c)>=0;
        }
        choice=5;
    }
    if(choice==6) {
        if(mark_candidates(c,p_crouch_dash_row)>=1) {
            P16(REC(0x40),0x3C);
            return execute_candidate(c)>=0;
        }
        choice=3;
    }
    if(choice==2) {
        const int32_t band=S32(REC(0x14));
        int32_t q,shorter,longer;
        if(0<=band && band<3) {q=random_below(c,8);shorter=q+4;longer=q+0x2C;}
        else if(band==3) {q=random_below(c,10);shorter=q+10;longer=q+0x32;}
        else {q=random_below(c,0xF);shorter=q+0xF;longer=q+0x37;}
        P16(REC(0x3A),0);P16(REC(0x38),shorter);P16(REC(0x40),longer);
        P16(REC(4),S16(REC(0x3C))>0?0x6000:0x2000);
        return 1;
    }
    if(choice==3) {
        const int32_t q=random_below(c,5);
        P16(REC(0x38),q+0x14);P16(REC(0x3A),0);P16(REC(0x40),q+0x3C);
        P16(REC(4),S16(REC(0x3C))>0?0xC000:0x8000);
        return 1;
    }
    if(choice==5) {
        const int32_t q=random_below(c,0x14);
        P16(REC(0x38),q+0x28);P16(REC(0x3A),0);P16(REC(0x40),q+0x50);
        P16(REC(4),S16(REC(0x3C))>0?0x6000:0x2000);
        return 1;
    }
    if(choice==8 && (S32(REC(0x18))>2 || S16(REC(0x20A))>10))side_step(c,-1);
    return execute_candidate(c)>=0;
}
/* LAB_80060944: a random movement by distance band (or, while waiting long,
 * by closing band). */
static int pick_movement(ctx_t* c) {
    probe(c,0x80060944);
    if(S16(REC(0x40))>=0 || S16(REC(0x48))>=0)return 0;
    const int32_t v=random_below(c,0x1000);
    int32_t choice=0;
    if(U8(REC(0x200))==0) {
        if(S16(REC(0x68))<0x24)choice=by_distance(c,v);
        else {
            choice=by_closing(c,v);
            if((choice==3 || choice==6) && S16(REC(0x284))<=(int32_t)(lcg_next(c)&0xFFF))choice=0;
        }
        if((choice==3 || choice==6) && ((U32(REC(0x230))&0x80000u) || U32(REC(0x234))!=0)
           && (int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x27E)))
            choice=0;
    }
    return do_movement(c,choice);
}
static int movement(ctx_t* c) {
    const uint32_t f=c->f,o=c->o;
    if(S16(REC(0x8A))>=0)return 0;
    if(S16(REC(0x38))<0) {
        if(S16(REC(0x50))==0 && S16(REC(0x4A))<0 && S16(REC(0x48))<0 && S16(REC(0x46))<0)return pick_movement(c);
        return 0;
    }
    if((U8(REC(0x200))==0 || S32(REC(0x1C))>7000) && S32(REC(0x14))>0) {
        const uint32_t x=U32(T3_PSX_AI_LCG),x1=5u*x+3u;
        if(S16(REC(0x27E))<=(int32_t)(x&0xFFF)) {
            int unsafe=0;
            if(S16(REC(0x20C))!=0 && S16(REC(0x6A))==0 && S16(f+0x3E)<0x5000 && S32(REC(0x1C))<0xB87)
                unsafe=(reach_bits(c,U32(o+0x54))&U32(APPROACH_REACH+4u*(uint32_t)S32(REC(0x18))))!=0;
            P32(T3_PSX_AI_LCG,x1);
            if(unsafe) {reset_approach(c);return pick_movement(c);}
        }
        P32(T3_PSX_AI_LCG,x1);
        if(S16(REC(0x3A))==2) {
            uint32_t pad;
            if(S32(REC(0x18))<3 && (lcg_next(c)&0xFFF)<0x20) {reset_approach(c);pad=0x8000;}
            else pad=0x2000;
            P16(REC(4),pad);
            return 1;
        }
        if(S16(REC(0x68))<0x24 && S32(REC(0x18))==2 && (lcg_next(c)&0xF)==0) {
            if(mark_candidates(c,p_side_step_row)>0 && execute_candidate(c)>=0) {
                P16(REC(0x3A),0);P16(REC(0x38),0);P16(REC(0x40),0x28);
                return 1;
            }
        }
        P16(REC(4),U16(REC(6))&0xF000u);
        return 1;
    }
    reset_approach(c);
    return pick_movement(c);
}
/* LAB_800605EC..LAB_80061108. */
static void move(ctx_t* c) {
    if(movement(c)==0) {
        if(S16(REC(0x4A))>=0)P16(REC(4),0x4000);
        else if(S16(REC(0x48))>=0)P16(REC(4),U16(REC(6))&0xF000u);
        else P16(REC(4),0);
    }
}

/* After the opponent whiffs, a draw marks a punish chance (+0x8F); otherwise
 * hold the current direction while the guard state (+0x6A) says so. */
static int hold_guard(ctx_t* c) {
    if(U8(c->o+0xD2)) {
        const int64_t threshold=(int64_t)S16(REC(0x258))
            +(int32_t)((uint32_t)S16(REC(0x216))*U16(T3_PSX_AI_DIFFICULTY)*30u);
        if((int32_t)(lcg_next(c)&0xFFF)<threshold) {P8(REC(0x8F),1);return 0;}
    }
    const int32_t first=c->op_first;
    if((S16(REC(0x6C))>=0 || first==0 || first-0x1E<c->op_frame) && S16(REC(0x6A))!=0) {
        P16(REC(4),U16(REC(6))&0xF000u);
        return 1;
    }
    return 0;
}

static void decide(ctx_t* c) {
    situation(c);
    if(fixed_input(c))return;
    track(c);
    probe(c,0x8005A9DC);
    if(throws(c) || stance(c))return;
    if(U32(c->f+0x1A4))return; /* a move row is queued */
    if(crouch_and_actions(c))return;
    if(call_hook(c,0) || call_hook(c,1))return;
    if(hold_guard(c) || new_move(c) || follow_through(c) || counter(c))return;
    probe(c,0x8005C514);
    if(react(c,reaction_kind(c)))return;
    expire_lists(c);
    if(avoid_air_attack(c))return;
    probe(c,0x8005E568);
    if(attack(c))return;
    probe(c,0x800605F4);
    move(c);
}

static void raise_capped(ctx_t* c,uint32_t address,int32_t step,uint32_t cap) {
    P16(address,h16(S16(address)+step));
    if(S16(cap)<S16(address))P16(address,S16(cap));
}
/* The CPU lost health: shorten the wait and remember the opponent's move; a
 * move seen twice switches to level 9 for 600 frames. */
static void learn_from_damage(ctx_t* c) {
    if((int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x27E)))return;
    P16(REC(0x68),S16(REC(0x68))-3);
    const uint32_t move=U32(c->o+0x54);
    if(U32(T3_PSX_AI_MULTI))return;
    P16(REC(0x202),0);
    uint32_t seen=0;
    for(uint32_t k=0;k<8;++k)seen+=U32(REC(0x1DC)+4u*k)==move;
    if(seen>1) {
        if(S16(REC(0x1FE))<1)load_params(c,0,9,-1);
        P16(REC(0x1FE),600);
        P16(REC(0x202),1);
    }
    const uint32_t k=U16(REC(0x1FC));
    P32(REC(0x1DC)+4u*k,move);
    P16(REC(0x1FC),(k+1)&7);
}

/* LAB_80061154: timers, adaptive difficulty, guard memory and the pad. */
static void finish(ctx_t* c,uint16_t* held,uint16_t* pressed) {
    const uint32_t f=c->f,o=c->o;
    const uint32_t state=U32(REC(0x224)),target=U32(REC(0x5C));
    P16(REC(0x3C),(state&0x205u)==1);
    if(target && S16(REC(0x48))<0 && U8(target+0x19))P16(REC(0x48),U8(target+0x19));
    static const uint16_t timers[]={0x48,0x38,0x40,0x54,0x46,0x44,0x42,0x8A,0x8C};
    for(unsigned k=0;k<9;++k)dec(c,REC(timers[k]));
    if(S16(REC(0x1FE))==0)load_params(c,-1,-1,-1);
    dec(c,REC(0x1FE));
    if(U32(T3_PSX_AI_GAME_MODE)==8) {
        const uint32_t slot=U16(REC(0));
        for(uint32_t base=FORCE_TARGETS;base<=FORCE_TARGETS+8;base+=8) {
            const int32_t v=S32(base+4u*slot);
            P32(base+4u*slot,v>=0?v-1:v);
        }
    }
    const uint32_t posture=state&0xFFFFu;
    P16(REC(0x212),S16(REC(0x20C)));
    const int guarding=posture==GUARD_HIGH || posture==GUARD_LOW;
    if(S16(REC(0x3E))==0) {
        P16(REC(0x3E),guarding);
        P8(REC(0x93),0);
    } else {
        P16(REC(0x3E),guarding);
        if(!guarding && U8(REC(0x93)))P8(REC(0x93),0);
    }
    if(U8(f+0xCE)) { /* got hit */
        reset_approach(c);
        static const uint16_t lists[]={0xD8,0xC0,0x1A0,0x98};
        for(unsigned k=0;k<4;++k)P16(REC(lists[k]),0);
    }
    if(U8(f+0xD2) && S32(REC(0x18))<3)P16(REC(0x54),0x1E);
    if(U8(o+0xD2) && S32(REC(0x14))<2 && (int32_t)(lcg_next(c)&0xFFF)<S16(REC(0x258)))
        P16(REC(0x68),S16(REC(0x68))-3);
    if(S32(f+0x3F4)<S32(REC(0x30)))learn_from_damage(c);
    P32(REC(0x30),S32(f+0x3F4));
    if(U8(f+0xCE) && U32(T3_PSX_AI_TEAM_OR_SURVIVAL)==0) {
        P16(REC(0x40),0x28);P16(REC(0x48),-1);P16(REC(0x3A),0);P16(REC(0x38),0);
        raise_capped(c,REC(0x254),10,REC(0x256));
        raise_capped(c,REC(0x258),10,REC(0x25A));
        if(S16(REC(0x216))>0) {
            raise_capped(c,REC(0x262),0x28,REC(0x264));
            raise_capped(c,REC(0x25E),0x14,REC(0x260));
        }
    }
    const uint32_t pad=U16(REC(4));
    *pressed=(uint16_t)(pad&(pad^U16(REC(6))));
    P16(REC(6),pad);
    *held=(uint16_t)pad;
}

void t3_psx_ai_update(const t3_psx_ai_bus_t* bus,uint32_t fighter,int has_stale,uint32_t stale,
                      uint16_t* held,uint16_t* pressed) {
    ctx_t context={0},*c=&context;
    c->bus=bus;c->f=fighter;c->has_stale=has_stale;c->stale=stale;
    c->rec=T3_PSX_AI_RECORDS+T3_PSX_AI_RECORD_SIZE*U8(fighter+0x1886);
    c->o=U32(REC(0x10));
    P32(T3_PSX_AI_OPP,c->o);
    P32(T3_PSX_AI_SELF,fighter);
    decide(c);
    finish(c,held,pressed);
}
