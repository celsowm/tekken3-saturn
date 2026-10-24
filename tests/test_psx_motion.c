#include "psx_motion.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Entire fixture is synthetic. Common bank has one move row and one
 * tagged 12-frame animation with 49 channels in section 6. */
static uint8_t bytes[512];
static uint8_t common_bytes[512];
static void put16(uint8_t* p,unsigned value) {
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8u);
}
static void put32(uint8_t* p,uint32_t value) {
    put16(p,value);put16(p+2,value>>16u);
}
static size_t make(uint8_t* data,unsigned kind) {
    unsigned i;
    const unsigned section6=132u;
    uint8_t* a=data+section6;
    size_t pos;
    memset(data,0,512u);
    data[1]=(uint8_t)kind;
    put16(data+2,1);
    put32(data+4,64u);
    put32(data+8,76u);
    put32(data+12,132u);
    for(i=3u;i<=6u;++i)put32(data+4u+i*4u,section6);
    /* Tagged stream begins at section6+2, move animation word=2. */
    put16(a,0x1234u);
    a[2]=12u;       /* frame count 12 */
    a[3]=12u;       /* channel0: 12 direct keys */
    a[4]=2u;        /* channel1: linear reversed */
    a[5]=1u;        /* channel2: constant */
    a[7]=3u;        /* channel4: keyed B-spline */
    pos=section6+2u+50u;
    for(i=0;i<12u;++i) {
        put16(data+pos,100u+i);pos+=2u;
    }
    put16(data+pos,1000u);pos+=2u;
    put16(data+pos,32u);pos+=2u;
    put16(data+pos,(uint16_t)-12);pos+=2u;
    put16(data+pos,100u);pos+=2u;
    put16(data+pos,36u);pos+=2u;  /* gap 5, +2 */
    put16(data+pos,69u);pos+=2u;  /* gap 6, +4; sum 11 */
    for(i=7u;i<15u;++i)put32(data+4u+i*4u,(uint32_t)pos);
    return pos;
}
static void open_and_sample(void) {
    t3_divmot_t bank={0},common={0};
    t3_motion_stream_t stream={0};
    int16_t xyz[3]={0},pose[T3_PSX_POSE_CHANNELS]={0};
    t3_motion_tables_t tab;
    static uint8_t table[T3_PSX_SPLINE_TABLE_BYTES]={0};
    size_t size=make(bytes,99u);
    assert(t3_divmot_open(&bank,bytes,size));
    assert(t3_motion_open(&bank,NULL,2u,&stream)==1);
    assert(stream.frames==12u && stream.tag==0x1234 && stream.bank_type==99u);
    assert(stream.counts[0]==12u && stream.counts[4]==3u);
    assert(t3_motion_sample_root(&stream,0u,NULL,xyz)==1);
    assert(xyz[0]==200 && xyz[1]==2022 && xyz[2]==-24);
    assert(t3_motion_sample_root(&stream,11u,NULL,xyz)==1);
    assert(xyz[0]==222 && xyz[1]==2000 && xyz[2]==-24);
    assert(t3_motion_sample_root(&stream,100u,NULL,xyz)==1);
    assert(xyz[0]==222);
    assert(t3_motion_sample(&stream,0u,NULL,pose)==0);
    /* Without explicit coefficients the scalar spline must not be guessed.
     * Table base 0x80096B40 starts at array offset 0x40. */
    put16(table+0x40+0x504,0x8000u);
    tab.blob=table;tab.length=sizeof(table);
    assert(t3_motion_sample(&stream,0u,&tab,pose)==1);
    assert(pose[0]==200 && pose[1]==2022 && pose[2]==-24);
    assert(pose[4]==202);  /* keyed base 100 + half of 2 */
    assert(pose[20]==0);
    assert(t3_motion_sample(&stream,11u,&tab,pose)==1);
    assert(pose[0]==222);
    /* At the final knot, 0x8000 + 0x5555 + 0x2AAB = 0x10000.
     * Keep that coefficient in32bits: channel4 must reach authored106,
     * doubled212, rather than lose the last increment and return204. */
    put16(table+0x40+0x300,0x5555u);
    assert(t3_motion_sample(&stream,11u,&tab,pose)==1);
    assert(pose[4]==212);
    assert(t3_motion_sample(&stream,0u,NULL,pose)==0);
    tab.length=0;
    assert(t3_motion_sample(&stream,0u,&tab,pose)==0);

    /* Common animation 0x40000000+2 must use common bank, not local. */
    size_t cs=make(common_bytes,99u);
    assert(t3_divmot_open(&common,common_bytes,cs));
    assert(t3_motion_open(&bank,&common,0x40000002u,&stream)==1);
    assert(stream.bank_type==99);
    assert(t3_motion_open(&bank,NULL,0x40000002u,&stream)==-1);
    assert(t3_motion_open(&bank,&common,0x80000002u,&stream)==-1);
    assert(t3_motion_open(&bank,NULL,0u,&stream)==-1);
    assert(t3_motion_open(&bank,NULL,999999u,&stream)==-1);
}
static void cache_ownership(void) {
    t3_divmot_t bank={0},common={0};
    t3_motion_cache_t cache={0};
    const t3_motion_stream_t *view=NULL,*again=NULL;
    int16_t root[3];
    assert(t3_divmot_open(&bank,bytes,make(bytes,99)));
    assert(t3_divmot_open(&common,common_bytes,make(common_bytes,99)));
    assert(t3_motion_open_cached(&cache,&bank,&common,2,&view)==1);
    assert(t3_motion_open_cached(&cache,&bank,&common,2,&again)==1 && again==view);
    assert(t3_motion_sample_root(view,0,NULL,root)==1 && root[0]==200);
    assert(t3_motion_open_cached(&cache,&bank,&common,0x40000002,&view)==1);
    assert(view->data==common_bytes && view->bank_type==99);
    assert(t3_motion_open_cached(&cache,&bank,NULL,0x40000002,&view)==-1);
    assert(!view && !cache.valid); /* never expose a stale successful view */
    assert(t3_motion_open_cached(&cache,&bank,&common,2,&view)==1);
    assert(view->data==bytes);
    cache=(t3_motion_cache_t){0}; /* explicit invalidation after a bank edit */
    bytes[134]=0;
    assert(t3_motion_open_cached(&cache,&bank,&common,2,&view)==-1 && !view);
}
static void decoded_sample_cache(void) {
    static t3_motion_sample_cache_t cache;
    static uint8_t table[T3_PSX_SPLINE_TABLE_BYTES];
    t3_motion_tables_t tables={table,sizeof(table)};
    t3_divmot_t bank={0};t3_motion_stream_t stream;
    int16_t expected[T3_PSX_POSE_CHANNELS],actual[T3_PSX_POSE_CHANNELS];
    size_t size=make(bytes,99);
    assert(t3_divmot_open(&bank,bytes,size));
    assert(t3_motion_open(&bank,NULL,2,&stream)==1);
    t3_motion_sample_cache_reset(&cache);
    for(unsigned pass=0;pass<2;++pass)for(unsigned frame=0;frame<12;++frame) {
        assert(t3_motion_sample(&stream,frame,&tables,expected)==1);
        assert(t3_motion_sample_cached(&cache,&stream,frame,&tables,actual)==1);
        assert(!memcmp(expected,actual,sizeof(actual)));
    }
    assert(cache.hits==12 && cache.misses==12);
    assert(t3_motion_sample_cached(&cache,&stream,1000,&tables,actual)==1);
    assert(cache.hits==13 && !memcmp(expected,actual,sizeof(actual)));
    // Different streams in one bank may share frame count and bank pointer.
    // Their first key locations identify them, even when all keys are zero.
    t3_motion_stream_t other=stream;
    for(unsigned ch=0;ch<T3_PSX_POSE_CHANNELS;++ch)other.counts[ch]=0;
    other.key_offset[0]=stream.key_offset[0]+128;
    assert(t3_motion_sample_cached(&cache,&other,11,&tables,actual)==1);
    for(unsigned ch=0;ch<T3_PSX_POSE_CHANNELS;++ch)assert(actual[ch]==0);
    assert(t3_motion_sample_cached(&cache,&stream,11,&tables,actual)==1);
    assert(!memcmp(expected,actual,sizeof(actual)));
    // An unavailable table must not reuse a successful spline sample.
    memset(actual,0x42,sizeof(actual));memcpy(expected,actual,sizeof(actual));
    assert(t3_motion_sample_cached(&cache,&stream,0,NULL,actual)==0);
    assert(t3_motion_sample_cached(&cache,&stream,0,NULL,actual)==0);
    assert(!cache.hits && cache.misses==2 && !memcmp(expected,actual,sizeof(actual)));
    assert(t3_motion_sample_cached(&cache,&stream,0,&tables,actual)==1);
    tables.length=0;
    assert(t3_motion_sample_cached(&cache,&stream,0,&tables,actual)==0);
    tables.length=sizeof(table);
    // Reloaded bytes at the same address require explicit invalidation.
    make(bytes,99);put16(bytes+184,321);
    t3_motion_sample_cache_reset(&cache);
    assert(t3_motion_sample_cached(&cache,&stream,0,&tables,actual)==1 && actual[0]==642);
    assert(t3_motion_sample_cached(NULL,&stream,0,&tables,expected)==1);
    assert(!memcmp(expected,actual,sizeof(actual)));
    assert(t3_motion_sample_cached(&cache,NULL,0,&tables,actual)==-1);
    assert(t3_motion_sample_cached(&cache,&stream,0,&tables,NULL)==-1);
}
static void malformed_streams(void) {
    t3_divmot_t bank={0};
    t3_motion_stream_t stream={0};
    size_t size=make(bytes,99u);
    assert(t3_divmot_open(&bank,bytes,size));
    bytes[134]=0; /* zero frame count */
    assert(t3_motion_open(&bank,NULL,2u,&stream)==-1);
    bytes[134]=12;
    bytes[135]=255; /* keys would extend outside section */
    assert(t3_motion_open(&bank,NULL,2u,&stream)==-1);
    bytes[135]=12;
    bytes[139]=3;
    bytes[134+2+50+24+4+2+2]=0; /* corrupt keyed last knot */
    assert(t3_motion_open(&bank,NULL,2u,&stream)==-1);
    /* The header open skips only the knot sums. */
    assert(t3_motion_open_header(&bank,NULL,2u,&stream)==1 && stream.frames==12u);
    bytes[135]=255;
    assert(t3_motion_open_header(&bank,NULL,2u,&stream)==-1);
    bytes[135]=12;
    size=make(bytes,99u);
    bytes[134+50+24+4+2+2]=0; /* corrupt first delta lower nibble */
    assert(t3_motion_open(&bank,NULL,2u,&stream)==-1);
    assert(t3_motion_open(NULL,NULL,2u,&stream)==-1);
    assert(t3_motion_sample(NULL,0u,NULL,NULL)==-1);
}
/* Random spline streams: every channel keyed, its spans summing to frames-1
 * as t3_motion_open requires, and a synthetic basis table. */
static uint32_t lcg=12345u;
static unsigned rnd(unsigned n) {lcg=lcg*1103515245u+12345u;return (lcg>>8)%n;}
static uint8_t keyed[2][T3_PSX_POSE_CHANNELS*256*2];
static void random_stream(t3_motion_stream_t* s,uint8_t* data,unsigned frames) {
    size_t pos=0;
    memset(s,0,sizeof(*s));
    s->data=data;s->length=sizeof(keyed[0]);s->frames=(uint8_t)frames;
    for(unsigned ch=0;ch<T3_PSX_POSE_CHANNELS;++ch) {
        /* Knots 1..k-1 carry spans of 1..32 adding up to frames-1. */
        const unsigned most=frames-1u<60u?frames-1u:60u,least=(frames-1u+31u)/32u;
        unsigned k=1u+least+rnd(most-least+1u),left=frames-1u;
        if(k<3u)k=3u;
        if(k-1u>frames-1u || k==frames)k=frames>3u?frames-1u:3u;
        s->counts[ch]=(uint8_t)k;s->key_offset[ch]=(uint32_t)pos;
        put16(data+pos,rnd(65536u));
        for(unsigned i=1;i<k;++i) {
            const unsigned after=k-1u-i,low=left>32u*after?left-32u*after:1u;
            const unsigned high=left-after<32u?left-after:32u;
            const unsigned span=low+rnd(high-low+1u);
            put16(data+pos+2u*i,(rnd(2048u)<<5)|(span-1u));
            left-=span;
        }
        assert(left==0u);
        pos+=2u*k;
    }
}
static unsigned sampled,compared;
static void same_sample(const t3_motion_stream_t* s,unsigned frame,
                        const t3_motion_tables_t* t,t3_motion_cursor_t* cursor) {
    int16_t expected[T3_PSX_POSE_CHANNELS],actual[T3_PSX_POSE_CHANNELS];
    const int a=t3_motion_sample(s,frame,t,expected),b=t3_motion_sample_cursor(s,frame,t,cursor,actual);
    assert(a==b);
    if(a==1)assert(!memcmp(expected,actual,sizeof(expected))),++sampled;
    ++compared;
}
static void knot_cursor(void) {
    static uint8_t table[T3_PSX_SPLINE_TABLE_BYTES];
    const t3_motion_tables_t t={table,sizeof(table)};
    t3_motion_stream_t a,b;
    t3_motion_cursor_t cursor={0};
    for(unsigned i=0;i<sizeof(table);++i)table[i]=(uint8_t)rnd(256u);
    for(unsigned trial=0;trial<40u;++trial) {
        const unsigned frames=4u+rnd(252u);
        random_stream(&a,keyed[0],frames);
        random_stream(&b,keyed[1],4u+rnd(252u));
        for(unsigned f=0;f<frames;++f)same_sample(&a,f,&t,&cursor);       /* forward */
        for(unsigned f=frames;f-->0;)same_sample(&a,f,&t,&cursor);        /* backward */
        for(unsigned n=0;n<200u;++n) {                                    /* jumps, both streams */
            const t3_motion_stream_t* s=rnd(4u)?&a:&b;
            same_sample(s,rnd(s->frames+2u),&t,&cursor);
        }
    }
    printf("knot cursor: %u of %u samples succeeded and matched\n",sampled,compared);
    assert(sampled*10u>=compared*9u);
}
int main(void) {
    open_and_sample();
    malformed_streams();
    cache_ownership();
    decoded_sample_cache();
    knot_cursor();
    puts("PS1 49-channel motion layout and sparse spline sample: passed");
    return 0;
}
