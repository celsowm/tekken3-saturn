/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Camera spline rule adapted into C from the pinned Tekken3Remake
 * tools/research/motion.py and camera_scripts.py original-code ports. */
#include "sim_profile.h"
#include "psx_motion.h"
#include <saturn/int_math.h>
#include <limits.h>

#define TABLE_BASE 0x80096B40u
#define TABLE_START 0x80096B00u

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1]<<8u));
}
static int16_t signed16(uint16_t v) {
    return v<0x8000u ? (int16_t)v : (int16_t)((int32_t)v-0x10000);
}
static int32_t wrap32(uint32_t lo) {
    return lo<0x80000000u ? (int32_t)lo : -(int32_t)~lo-1;
}
static int32_t floor_shift(int32_t v,unsigned shift) {
    return sat_floor_shift_i32(v,shift);
}
static int get_byte(const t3_motion_tables_t* t, uint32_t addr, uint8_t* out) {
    size_t p;
    if (!t || !t->blob || !out || addr<TABLE_START) return 0;
    p=(size_t)(addr-TABLE_START);
    if (p>=t->length || p>=T3_PSX_SPLINE_TABLE_BYTES) return 0;
    *out=t->blob[p];
    return 1;
}
static int get_word(const t3_motion_tables_t* t, uint32_t addr, uint16_t* out) {
    uint8_t lo,hi;
    if (!get_byte(t,addr,&lo) || !get_byte(t,addr+1u,&hi)) return 0;
    *out=(uint16_t)((uint16_t)lo | ((uint16_t)hi<<8u));
    return 1;
}
static int16_t get_key(const t3_motion_stream_t* s,unsigned ch,unsigned k) {
    return signed16(le16(s->data+s->key_offset[ch]+(size_t)k*2u));
}

static int open_stream(const t3_divmot_t* charbank, const t3_divmot_t* common,
                       uint32_t word, t3_motion_stream_t* out,int knots) {
    const t3_divmot_t* bank;
    t3_motion_stream_t temp={0};
    size_t start,end,off,total;
    unsigned i;
    if (!charbank || !charbank->data || !out) return -1;
    bank=charbank;
    if (word>=0x40000000u) {
        if (word>=0x80000000u || !common || common->bank_type!=99u ||
            !common->data) return -1;
        bank=common;
        word-=0x40000000u;
    }
    start=bank->sections[6];
    end=bank->sections[7];
    if (end>bank->size || start>end) return -1;
    if (start==end) return 0;
    if (word> end-start || end-start-word<50u) return -1;
    off=start+(size_t)word;
    if (off<start+2u) return -1; /* s16 tag immediately before stream */
    temp.data=bank->data;
    temp.length=bank->size;
    temp.frames=bank->data[off];
    temp.counts[0]=bank->data[off+1];
    temp.tag=signed16(le16(bank->data+off-2u));
    temp.bank_type=bank->bank_type;
    if (temp.frames==0u) return -1;
    for (i=1u;i<T3_PSX_POSE_CHANNELS;++i)temp.counts[i]=bank->data[off+1u+i];
    total=off+50u;
    for(i=0;i<T3_PSX_POSE_CHANNELS;++i) {
        unsigned j,sum;
        uint8_t n=temp.counts[i];
        temp.key_offset[i]=(uint32_t)total;
        if ((size_t)n>(end-total)/2u) return -1;
        if (knots && n>2u && n!=temp.frames) {
            /* Each knot's span is its little-endian low byte's bits 0..4, plus one. */
            const uint8_t* key=bank->data+total+2u;
            sum=n-1u;
            for(j=1u;j<n;++j,key+=2)sum+=(unsigned)(*key&31u);
            if(sum!=(unsigned)temp.frames-1u) return -1;
        }
        total+=(size_t)n*2u;
    }
    *out=temp;
    return 1;
}
int t3_motion_open(const t3_divmot_t* charbank, const t3_divmot_t* common,
                   uint32_t word, t3_motion_stream_t* out) {
    return open_stream(charbank,common,word,out,1);
}
int t3_motion_open_header(const t3_divmot_t* charbank, const t3_divmot_t* common,
                          uint32_t word, t3_motion_stream_t* out) {
    return open_stream(charbank,common,word,out,0);
}

int t3_motion_open_cached(t3_motion_cache_t* cache,const t3_divmot_t* bank,
                          const t3_divmot_t* common,uint32_t word,
                          const t3_motion_stream_t** out) {
    int opened;
    if(!cache || !out)return -1;
    *out=NULL;
    if(cache->valid && cache->bank==bank && cache->common==common && cache->word==word) {
        *out=&cache->stream;return 1;
    }
    cache->valid=0;
    opened=t3_motion_open(bank,common,word,&cache->stream);
    if(opened==1) {
        cache->bank=bank;cache->common=common;cache->word=word;cache->valid=1;
        *out=&cache->stream;
    }
    return opened;
}

/* The knot walk at the top of the original's loop: the next key to read,
 * the segment [prev, cur] and the points p0..p2 (p3 equals p2 there). The
 * walk stops at the first knot whose segment reaches the frame. */
typedef struct { int32_t p0,p1,p2; unsigned prev,cur,next; } walk_t;
static void walk_start(const t3_motion_stream_t* s,unsigned ch,int32_t scale,walk_t* w) {
    const int32_t delta=get_key(s,ch,1);
    w->p0=w->p1=get_key(s,ch,0);
    /* 1<<1 or 1<<5 as a constant: the SH-2 has no variable shift. */
    w->p2=w->p1+floor_shift(delta,5u)*scale;
    w->prev=0u;w->cur=(unsigned)(delta&31)+1u;w->next=2u;
}
static int walk_stops(const walk_t* w,unsigned frames,unsigned k,unsigned frame) {
    return w->cur==frames-1u || w->next>=k || frame<=w->cur;
}
static void walk_forward(const t3_motion_stream_t* s,unsigned ch,int32_t scale,walk_t* w) {
    const int32_t delta=get_key(s,ch,w->next++);
    w->prev=w->cur;w->cur+=(unsigned)(delta&31)+1u;
    w->p0=w->p1;w->p1=w->p2;w->p2+=floor_shift(delta,5u)*scale;
}
/* walk_forward undone: p1 is p0 plus the key before the last read, and prev
 * is cur less the last read key's span. */
static void walk_back(const t3_motion_stream_t* s,unsigned ch,int32_t scale,walk_t* w) {
    --w->next;
    w->cur=w->prev;w->p2=w->p1;w->p1=w->p0;
    w->prev=w->cur-((unsigned)(get_key(s,ch,w->next-1u)&31)+1u);
    w->p0=w->next>2u?w->p1-floor_shift(get_key(s,ch,w->next-2u),5u)*scale:w->p1;
}

/* PS1 keyed-channel B-spline. Weight tables are taken from a PRIVATE
 * executable or a synthetic fixture supplied by the caller. This is
 * intentionally the integer/table algorithm, not approximate catmull-rom.
 * `w` is where the knot walk stopped for this frame. */
static int evaluate(const t3_motion_stream_t* s, unsigned ch, unsigned frame,
                    const t3_motion_tables_t* t, int32_t scale, const walk_t* w,
                    int32_t* result) {
    const unsigned frames=s->frames,k=s->counts[ch],prev=w->prev,cur=w->cur;
    const int32_t p0=w->p0,p1=w->p1,p2=w->p2;
    int32_t p3=p2;
    unsigned span,local;
    uint16_t a0=0,a2=0,a3=0,lookup_word=0;
    uint32_t a1=0;
    uint8_t lookup;
    uint32_t key_base=TABLE_BASE, t8,t9;
    /* Only the low 32 bits of the weighted sum are used: products and sums
     * modulo 2^32 give it without 64-bit multiplies on the SH-2. */
    uint32_t weighted;
    /* The original's loop read the next key before it stopped on the frame. */
    if(cur!=frames-1u && w->next<k)p3+=floor_shift(get_key(s,ch,w->next),5u)*scale;
    /* Our parser validated knot sums. Do not access an undefined segment. */
    if(cur<prev || cur>=frames || frame<prev) return -1;
    local=frame-prev;
    span=cur-prev;
    if(local) {
        uint32_t addr;
        if(span<=16u)addr=key_base+span*32u+local-0x21u;
        else addr=key_base-span*32u-local+0x420u;
        if (!get_byte(t,addr,&lookup))return 0;
        local=(unsigned)lookup*2u;
    }
    t8=key_base+local;
    if(local>key_base)return -1;
    t9=key_base-local;
    if(prev==0u) {
        if (!get_word(t,t9+0x300u,&a0) ||
            !get_word(t,t9+0x504u,&lookup_word))return 0;
        a1=lookup_word;
        weighted=(uint32_t)a0*(uint32_t)(p3-p1);
    } else {
        if (!get_word(t,t8+0x200u,&a0))return 0;
        weighted=(uint32_t)a0*(uint32_t)(p0-p1);
        if(cur!=frames-1u) {
            if (!get_word(t,t9+0x300u,&a2) ||
                !get_word(t,t9+0x402u,&lookup_word))return 0;
            a1=lookup_word;
            weighted+=(uint32_t)a2*(uint32_t)(p3-p1);
        } else {
            if (!get_word(t,t9+0x504u,&lookup_word) ||
                !get_word(t,t9+0x300u,&a3))return 0;
            /* The final-segment coefficient is a 32-bit sum. It commonly
             * equals 0x10000 at the endpoint; narrowing to16 erased it. */
            a1=(uint32_t)lookup_word+a3+0x2AABu;
        }
    }
    weighted+=a1*(uint32_t)(p2-p1);
    *result=wrap32((uint32_t)floor_shift(wrap32(weighted),16u)+(uint32_t)p1);
    return 1;
}
static int spline(const t3_motion_stream_t* s, unsigned ch, unsigned frame,
                  const t3_motion_tables_t* t, int32_t scale, int32_t* result) {
    walk_t w;
    walk_start(s,ch,scale,&w);
    while(!walk_stops(&w,s->frames,s->counts[ch],frame))walk_forward(s,ch,scale,&w);
    return evaluate(s,ch,frame,t,scale,&w,result);
}
/* spline resuming from the knot this channel's last sample stopped at. The
 * walk stops where it would from the first knot: stepping back while the
 * knot before would stop, then forward until this one does. */
static int spline_from(const t3_motion_stream_t* s, unsigned ch, unsigned frame,
                       const t3_motion_tables_t* t, int32_t scale,
                       t3_motion_knot_t* knot, int32_t* result) {
    walk_t w;
    if(!knot->next)walk_start(s,ch,scale,&w);
    else w=(walk_t){knot->p0,knot->p1,knot->p2,knot->prev,knot->cur,knot->next};
    while(w.next>2u && frame<=w.prev)walk_back(s,ch,scale,&w);
    while(!walk_stops(&w,s->frames,s->counts[ch],frame))walk_forward(s,ch,scale,&w);
    *knot=(t3_motion_knot_t){w.p0,w.p1,w.p2,(uint16_t)w.prev,(uint16_t)w.cur,(uint8_t)w.next};
    return evaluate(s,ch,frame,t,scale,&w,result);
}
static int sample_one(const t3_motion_stream_t* s, unsigned ch,
                      unsigned frame, const t3_motion_tables_t* t,
                      t3_motion_knot_t* knot, int16_t* output) {
    unsigned n=s->frames,k=s->counts[ch],f;
    int32_t v=0;
    int rc;
    if(!n || ch>=T3_PSX_POSE_CHANNELS) return -1;
    if(frame>=n)frame=n-1u;
    f=(ch&1u)?n-1u-frame:frame;
    if(k==0u)v=0;
    else if(k==1u)v=get_key(s,ch,0);
    else if(k==2u) {
        int32_t delta=(int32_t)get_key(s,ch,1)*(int32_t)f;
        if(n>9u)delta=floor_shift(delta,5u);
        v=(int32_t)(uint16_t)get_key(s,ch,0)+delta;
    } else if(k==n)v=get_key(s,ch,f);
    else {
        rc=knot?spline_from(s,ch,f,t,ch<15u?2:32,knot,&v):spline(s,ch,f,t,ch<15u?2:32,&v);
        if(rc!=1)return rc;
    }
    *output=signed16((uint16_t)(((uint32_t)v*2u)&0xffffu));
    return 1;
}
int t3_motion_spline_words(const uint8_t* keys,size_t bytes,unsigned count,
                           unsigned frame,unsigned frames,const t3_motion_tables_t* tables,
                           int16_t* out) {
    t3_motion_stream_t stream={0};
    int32_t value;
    if(!keys || !out || count<3u || count>255u || frames<2u || frames>255u ||
       frame>=frames || bytes<count*2u)return -1;
    stream.data=keys;stream.length=bytes;stream.frames=(uint8_t)frames;
    stream.counts[0]=(uint8_t)count;
    if(spline(&stream,0,frame,tables,16,&value)!=1)return 0;
    *out=signed16((uint16_t)value);
    return 1;
}
int T3_SIM_IMPL(t3_motion_sample_cursor)(const t3_motion_stream_t* s, unsigned frame,
                     const t3_motion_tables_t* tables,t3_motion_cursor_t* cursor,
                     int16_t out[T3_PSX_POSE_CHANNELS]) {
    int16_t v[T3_PSX_POSE_CHANNELS];
    unsigned i;int result;
    if(!s || !out || !s->data || !s->frames)return -1;
    if(cursor) {
        /* The first key's address identifies the opened stream in its bank. */
        const uint8_t* identity=s->data+s->key_offset[0];
        if(cursor->stream!=identity || cursor->frames!=s->frames) {
            for(i=0;i<T3_PSX_POSE_CHANNELS;++i)cursor->knot[i].next=0;
            cursor->stream=identity;cursor->frames=s->frames;
        }
    }
    for(i=0;i<T3_PSX_POSE_CHANNELS;++i) {
        result=sample_one(s,i,frame,tables,cursor?&cursor->knot[i]:NULL,&v[i]);
        if(result!=1) {
            if(cursor)cursor->stream=NULL;
            return result;
        }
    }
    for(i=0;i<T3_PSX_POSE_CHANNELS;++i)out[i]=v[i];
    return 1;
}
int t3_motion_sample(const t3_motion_stream_t* s, unsigned frame,
                     const t3_motion_tables_t* tables,
                     int16_t out[T3_PSX_POSE_CHANNELS]) {
    return t3_motion_sample_cursor(s,frame,tables,NULL,out);
}
T3_SIM_WRAP(int,t3_motion_sample_cursor,T3_SIM_SAMPLE,
    (const t3_motion_stream_t* s,unsigned frame,
     const t3_motion_tables_t* tables,t3_motion_cursor_t* cursor,
     int16_t out[T3_PSX_POSE_CHANNELS]),
    (s,frame,tables,cursor,out))
int T3_SIM_IMPL(t3_motion_sample_root)(const t3_motion_stream_t* s,unsigned frame,
                          const t3_motion_tables_t* tables,int16_t xyz[3]) {
    int16_t tmp[3];unsigned i;int rc;
    if(!s || !xyz || !s->data || !s->frames)return -1;
    for(i=0;i<3u;++i) {
        rc=sample_one(s,i,frame,tables,NULL,&tmp[i]);
        if(rc!=1)return rc;
    }
    for(i=0;i<3u;++i)xyz[i]=tmp[i];
    return 1;
}
T3_SIM_WRAP(int,t3_motion_sample_root,T3_SIM_MOTION,
    (const t3_motion_stream_t* s,unsigned frame,
                          const t3_motion_tables_t* tables,int16_t xyz[3]),
    (s,frame,tables,xyz))

void t3_motion_sample_cache_reset(t3_motion_sample_cache_t* cache) {
    if(cache)__builtin_memset(cache,0,sizeof(*cache));
}
int T3_SIM_IMPL(t3_motion_sample_cached)(t3_motion_sample_cache_t* cache,
    const t3_motion_stream_t* s,unsigned frame,const t3_motion_tables_t* tables,
    int16_t pose[T3_PSX_POSE_CHANNELS]) {
    if(!cache)return t3_motion_sample(s,frame,tables,pose);
    if(!s || !pose || !s->data || !s->frames)return -1;
    const uint8_t* table=tables?tables->blob:0;
    size_t length=tables?tables->length:0;
    if(cache->table!=table || cache->table_length!=length) {
        t3_motion_sample_cache_reset(cache);
        cache->table=table;cache->table_length=length;
    }
    if(frame>=s->frames)frame=s->frames-1u;
    /* data is the whole bank; the first key offset uniquely identifies the
     * opened animation header within it, including all-zero channels. */
    const uint8_t* identity=s->data+s->key_offset[0];
    uint32_t hash=(uint32_t)((uintptr_t)identity>>2)*0x9E3779B1u;
    unsigned index=(hash^frame)&(T3_MOTION_SAMPLE_CACHE_SLOTS-1u);
    t3_motion_sample_entry_t* e=&cache->entries[index];
    if(e->valid && e->stream==identity && e->frame==frame && e->frames==s->frames) {
        __builtin_memcpy(pose,e->pose,sizeof(e->pose));
        t3_sim_count(T3_SIM_CACHE_HIT);
        ++cache->hits;return 1;
    }
    t3_sim_count(T3_SIM_CACHE_MISS);
    ++cache->misses;
    int result=t3_motion_sample(s,frame,tables,pose);
    if(result!=1)return result;
    __builtin_memcpy(e->pose,pose,sizeof(e->pose));
    e->stream=identity;e->frame=(uint16_t)frame;e->frames=s->frames;e->valid=1;
    return 1;
}
T3_SIM_WRAP(int,t3_motion_sample_cached,T3_SIM_MOTION,
    (t3_motion_sample_cache_t* cache,
    const t3_motion_stream_t* s,unsigned frame,const t3_motion_tables_t* tables,
    int16_t pose[T3_PSX_POSE_CHANNELS]),
    (cache,s,frame,tables,pose))
