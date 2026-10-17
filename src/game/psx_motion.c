#include "psx_motion.h"
#include "fixed_shift.h"
#include <limits.h>

#define TABLE_BASE 0x80096B40u
#define TABLE_START 0x80096B00u

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1]<<8u));
}
static int16_t signed16(uint16_t v) {
    return v<0x8000u ? (int16_t)v : (int16_t)((int32_t)v-0x10000);
}
static int32_t wrap32(int64_t v) {
    uint32_t lo=(uint32_t)(uint64_t)v;
    return lo<0x80000000u ? (int32_t)lo : (int32_t)((int64_t)lo-4294967296LL);
}
static int32_t floor_shift(int64_t v,unsigned shift) {
    return t3_floor_shift(v,shift);
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

int t3_motion_open(const t3_divmot_t* charbank, const t3_divmot_t* common,
                   uint32_t word, t3_motion_stream_t* out) {
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
        if (n>2u && n!=temp.frames) {
            sum=0;
            for(j=1u;j<n;++j)
                sum+=(unsigned)(le16(bank->data+total+2u*j)&31u)+1u;
            if(sum!=(unsigned)temp.frames-1u) return -1;
        }
        total+=(size_t)n*2u;
    }
    *out=temp;
    return 1;
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

/* PS1 keyed-channel B-spline. Weight tables are taken from a PRIVATE
 * executable or a synthetic fixture supplied by the caller. This is
 * intentionally the integer/table algorithm, not approximate catmull-rom. */
static int spline(const t3_motion_stream_t* s, unsigned ch, unsigned frame,
                  const t3_motion_tables_t* t, int32_t* result) {
    unsigned frames=s->frames,k=s->counts[ch],i=2u;
    int32_t p0=get_key(s,ch,0),p1=p0,p2,p3;
    int32_t delta=get_key(s,ch,1);
    unsigned span=(unsigned)(delta&31)+1u,prev=0u,cur=span,local;
    uint16_t a0=0,a2=0,a3=0,lookup_word=0;
    uint32_t a1=0;
    uint8_t lookup;
    uint32_t key_base=TABLE_BASE, t8,t9;
    int64_t weighted;
    int32_t shift=(ch<15u)?1:5;
    p3=p1+floor_shift(delta,5u)*(1<<shift);
    p2=p3;
    while(cur!=frames-1u && i<k) {
        delta=get_key(s,ch,i++);
        p3+=floor_shift(delta,5u)*(1<<shift);
        span=(unsigned)(delta&31)+1u;
        if(frame<=cur)break;
        prev=cur;
        cur+=span;
        p0=p1; p1=p2; p2=p3;
    }
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
        weighted=(int64_t)a0*(p3-p1);
    } else {
        if (!get_word(t,t8+0x200u,&a0))return 0;
        weighted=(int64_t)a0*(p0-p1);
        if(cur!=frames-1u) {
            if (!get_word(t,t9+0x300u,&a2) ||
                !get_word(t,t9+0x402u,&lookup_word))return 0;
            a1=lookup_word;
            weighted+=(int64_t)a2*(p3-p1);
        } else {
            if (!get_word(t,t9+0x504u,&lookup_word) ||
                !get_word(t,t9+0x300u,&a3))return 0;
            /* The final-segment coefficient is a 32-bit sum. It commonly
             * equals 0x10000 at the endpoint; narrowing to16 erased it. */
            a1=(uint32_t)lookup_word+a3+0x2AABu;
        }
    }
    weighted+=(int64_t)a1*(p2-p1);
    *result=wrap32((int64_t)floor_shift(wrap32(weighted),16u)+p1);
    return 1;
}
static int sample_one(const t3_motion_stream_t* s, unsigned ch,
                      unsigned frame, const t3_motion_tables_t* t,
                      int16_t* output) {
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
        rc=spline(s,ch,f,t,&v);
        if(rc!=1)return rc;
    }
    *output=signed16((uint16_t)(((uint32_t)v*2u)&0xffffu));
    return 1;
}
int t3_motion_sample(const t3_motion_stream_t* s, unsigned frame,
                     const t3_motion_tables_t* tables,
                     int16_t out[T3_PSX_POSE_CHANNELS]) {
    int16_t v[T3_PSX_POSE_CHANNELS];
    unsigned i;int result;
    if(!s || !out || !s->data || !s->frames)return -1;
    for(i=0;i<T3_PSX_POSE_CHANNELS;++i) {
        result=sample_one(s,i,frame,tables,&v[i]);
        if(result!=1)return result;
    }
    for(i=0;i<T3_PSX_POSE_CHANNELS;++i)out[i]=v[i];
    return 1;
}
int t3_motion_sample_root(const t3_motion_stream_t* s,unsigned frame,
                          const t3_motion_tables_t* tables,int16_t xyz[3]) {
    int16_t tmp[3];unsigned i;int rc;
    if(!s || !xyz || !s->data || !s->frames)return -1;
    for(i=0;i<3u;++i) {
        rc=sample_one(s,i,frame,tables,&tmp[i]);
        if(rc!=1)return rc;
    }
    for(i=0;i<3u;++i)xyz[i]=tmp[i];
    return 1;
}
void t3_motion_sample_cache_reset(t3_motion_sample_cache_t* cache) {
    if(cache)__builtin_memset(cache,0,sizeof(*cache));
}
int t3_motion_sample_cached(t3_motion_sample_cache_t* cache,
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
        ++cache->hits;return 1;
    }
    ++cache->misses;
    int result=t3_motion_sample(s,frame,tables,pose);
    if(result!=1)return result;
    __builtin_memcpy(e->pose,pose,sizeof(e->pose));
    e->stream=identity;e->frame=(uint16_t)frame;e->frames=s->frames;e->valid=1;
    return 1;
}
