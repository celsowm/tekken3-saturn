#include "psx_ik.h"
#include "psx_local_pose.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t sine[T3_PSX_TRIG_BYTES];
static uint8_t ik[T3_PSX_IK_BYTES];

static void wr16(uint8_t* p,uint16_t value) {
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);
}
static uint32_t isqrt32(uint32_t n) {
    uint32_t result=0,bit=1u<<30;
    while(bit>n)bit>>=2;
    while(bit) {
        if(n>=result+bit) {
            n-=result+bit;
            result=(result>>1)+bit;
        } else result>>=1;
        bit>>=2;
    }
    return result;
}
static void fixtures(void) {
    unsigned i;
    memset(sine,0,sizeof(sine));
    memset(ik,0,sizeof(ik));
    /* Synthetic cardinal angles: real table bytes are deliberately absent. */
    wr16(sine+0x0000,0);
    wr16(sine+0x0800,4096);
    wr16(sine+0x1000,0);
    wr16(sine+0x1800,(uint16_t)-4096);
    wr16(sine+0x2000,0);
    /* SquareRoot0 normalized mantissa = sqrt(x) * 512, rounded down.
     * Matches integer power-of-two rescaling, NOT retail lookup entries. */
    for(i=0;i<192u;++i)
        wr16(ik+i*2u,(uint16_t)isqrt32((i+0x40u)<<18));
    /* IK basis identity, no shifts of shoulder/hip or limb roots. */
    for(i=0;i<3u;++i)
        wr16(ik+T3_PSX_SQRT_BYTES+(i*3u+i)*2u,4096);
}
static int eq(const t3_psx_matrix_t* a,const t3_psx_matrix_t* b) {
    return memcmp(a,b,sizeof(*a))==0;
}
static void end_to_end(void) {
    t3_psx_ik_tables_t table={{sine,sizeof(sine)},ik,sizeof(ik)};
    t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS]={0};
    t3_psx_matrix_t saved[T3_PSX_ROTATION_SLOTS]={0};
    t3_psx_matrix_t expected[T3_PSX_ROTATION_SLOTS]={0};
    int16_t pose[T3_PSX_POSE_CHANNELS]={0};
    uint32_t mask=0xFFFFFFFFu;
    fixtures();
    pose[3]=600;pose[6]=600;
    pose[9]=960;pose[12]=960;
    assert(t3_psx_pose_full(pose,&table,slots,&mask)==1);
    assert(mask==0x3FFFFu); /* all 10 FK + 8 original IK slots */
    /* The original MIPS/GTE IK deliberately permits signed 16-bit
     * wrap/saturation beyond unit Q12 for near-limit limb targets.
     * Do not assert a fictitious [-4096,+4096] matrix-component bound. */
    assert(slots[0].m[0][0]==4096);
    assert(slots[1].m[1][1]==4096);
    assert(slots[11].m[2][2]==4096);
    /* For overextended limbs the PS1 lower-bone rotation is identity. */
    for(unsigned k=0;k<4;++k) {
        static const unsigned lower[4]={5,9,13,16};
        const unsigned idx=lower[k];
        assert(slots[idx].m[0][0]==4096);
        assert(slots[idx].m[1][1]==4096);
        assert(slots[idx].m[2][2]==4096);
    }
    for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i)
        expected[i]=slots[i];
    assert(t3_psx_pose_full(pose,&table,slots,&mask)==1);
    for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i)
        assert(eq(&slots[i],&expected[i]));
    /* A zero target is singular. The respective two slots must NOT be
     * manufactured as identity or inherited from a previous frame. */
    pose[3]=0;
    assert(t3_psx_pose_full(pose,&table,slots,&mask)==1);
    assert(mask==(0x3FFFFu &~((1u<<4)|(1u<<5))));
    assert(slots[4].m[0][0]==0);
    assert(slots[5].m[1][1]==0);

    /* In-range elbow bend exercises the square-root path a second time. */
    pose[3]=300;
    assert(t3_psx_pose_full(pose,&table,slots,&mask)==1);
    assert(mask==0x3FFFFu);
    assert(slots[5].m[2][2]==4096);
    /* A valid PS1 limb solution must be stable across identical samples. */
    for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i)saved[i]=slots[i];
    assert(t3_psx_pose_full(pose,&table,slots,&mask)==1);
    for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i)
        assert(eq(&saved[i],&slots[i]));
}
static void failure_contract(void) {
    t3_psx_ik_tables_t table={{sine,sizeof(sine)},ik,sizeof(ik)};
    t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS]={0};
    t3_psx_matrix_t snapshot[T3_PSX_ROTATION_SLOTS]={0};
    int16_t pose[49]={0};
    uint32_t mask=12345u;
    fixtures();
    pose[3]=600;pose[6]=600;pose[9]=960;pose[12]=960;
    table.ik_size=0;
    assert(t3_psx_pose_full(pose,&table,slots,&mask)==0);
    assert(mask==12345u);
    assert(memcmp(slots,snapshot,sizeof(slots))==0);
    table.ik_size=sizeof(ik);
    table.trig.size=12u;
    assert(t3_psx_pose_full(pose,&table,slots,&mask)==0);
    table.trig.size=sizeof(sine);
    assert(t3_psx_pose_full(NULL,&table,slots,&mask)==-1);
    assert(t3_psx_pose_full(pose,&table,NULL,&mask)==-1);
    assert(t3_psx_pose_full(pose,&table,slots,NULL)==-1);
    /* A bad source FK validity mask must not get auto-filled with IK. */
    assert(t3_psx_pose_ik(pose,&table,slots,0u,slots,&mask)==-1);
    assert(mask==12345u);
}
/* A raw stream of N frames: channel 3 (odd, so read in reverse) holds one key
 * per frame, the other limb channels are constant. Frame 0 reads key N-1,
 * which is zero: a singular limb. */
enum {STREAM_FRAMES=200};
static uint8_t stream_bytes[(STREAM_FRAMES+8)*2];
static t3_motion_stream_t make_stream(void) {
    t3_motion_stream_t s={0};
    s.data=stream_bytes;s.length=sizeof(stream_bytes);s.frames=STREAM_FRAMES;
    for(unsigned f=0;f<STREAM_FRAMES;++f)
        wr16(stream_bytes+8+f*2u,(uint16_t)(f==STREAM_FRAMES-1u?0:150u+f));
    wr16(stream_bytes+0,300);wr16(stream_bytes+2,480);
    s.counts[3]=STREAM_FRAMES;s.key_offset[3]=8;
    s.counts[6]=1;s.key_offset[6]=0;
    s.counts[9]=s.counts[12]=1;s.key_offset[9]=s.key_offset[12]=2;
    return s;
}
static void expect(const t3_motion_stream_t* s,unsigned frame,const t3_psx_ik_tables_t* table,
                   int16_t pose[T3_PSX_POSE_CHANNELS],t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],
                   uint32_t* mask) {
    assert(t3_motion_sample(s,frame,NULL,pose)==1);
    assert(t3_psx_pose_full(pose,table,slots,mask)==1);
}
static void local_pose_cache(void) {
    static t3_psx_local_pose_entry_t entries[256];
    t3_psx_ik_tables_t table={{sine,sizeof(sine)},ik,sizeof(ik)};
    t3_psx_local_pose_cache_t cache;
    t3_motion_stream_t stream=make_stream();
    t3_psx_matrix_t expected[T3_PSX_ROTATION_SLOTS],actual[T3_PSX_ROTATION_SLOTS];
    int16_t want[T3_PSX_POSE_CHANNELS],pose[T3_PSX_POSE_CHANNELS],root;
    uint32_t expected_mask,mask,fk;
#define LOCAL(s,frame,c) t3_psx_local_pose((s),(frame),NULL,&table.trig,&table,(c),pose,actual,&fk,&mask,&root)
#define SAME() (mask==expected_mask && !memcmp(expected,actual,sizeof(actual)) && \
                !memcmp(want,pose,sizeof(pose)))
    fixtures();
    t3_psx_local_pose_cache_init(&cache,entries,2); /* one set of two ways */
    assert(cache.sets==1 && cache.ways==2);
    expect(&stream,5,&table,want,expected,&expected_mask);
    assert(LOCAL(&stream,5,&cache));
    assert(cache.misses==1 && !cache.hits && SAME());
    assert(fk==0x24CCFu && root==4096 && pose[3]==2*(150+194));
    /* Mutating the returned buffers cannot corrupt the retained entry. */
    memset(actual,0,sizeof(actual));memset(pose,0,sizeof(pose));
    assert(LOCAL(&stream,5,&cache) && cache.hits==1 && SAME());
    /* Frames clamp to N-1 before keying; frame 0 is a singular limb whose
     * incomplete mask is retained exactly. */
    expect(&stream,0,&table,want,expected,&expected_mask);
    assert(expected_mask!=0x3FFFFu);
    assert(LOCAL(&stream,0,&cache) && cache.misses==2 && SAME());
    assert(LOCAL(&stream,0,&cache) && cache.hits==2 && SAME());
    expect(&stream,STREAM_FRAMES-1u,&table,want,expected,&expected_mask);
    assert(LOCAL(&stream,1000,&cache) && cache.misses==3 && cache.evictions==1 && SAME());
    /* The way used last survives: frame 5 was replaced, not frame 0. */
    assert(LOCAL(&stream,STREAM_FRAMES-1u,&cache) && cache.hits==3 && SAME());
    expect(&stream,5,&table,want,expected,&expected_mask);
    assert(LOCAL(&stream,5,&cache) && cache.hits==3 && cache.misses==4 && SAME());
    /* Another stream in the same bytes is told apart by its first key. */
    t3_motion_stream_t other=stream;
    other.key_offset[0]=4;
    assert(LOCAL(&other,5,&cache) && cache.misses==5 && SAME());
    /* Changing table bounds invalidates a previous hit and fails atomically. */
    memcpy(want,pose,sizeof(pose));memcpy(expected,actual,sizeof(expected));
    table.ik_size=0;fk=123;mask=456;root=789;
    assert(!LOCAL(&stream,5,&cache));
    assert(fk==123 && mask==456 && root==789 && !memcmp(expected,actual,sizeof(actual)) &&
           !memcmp(want,pose,sizeof(pose)));
    table.ik_size=sizeof(ik);
    assert(LOCAL(&stream,5,&cache) && cache.hits==3 && cache.misses==7);
    t3_psx_local_pose_cache_init(&cache,entries,2); /* Same-address bytes reload. */
    assert(!cache.hits && !cache.misses && !entries[0].stream && !entries[1].stream);
    expect(&stream,7,&table,want,expected,&expected_mask);
    assert(LOCAL(&stream,7,NULL) && SAME());
    /* Two hundred frames fit 128 two-way sets: the second pass only hits. */
    t3_psx_local_pose_cache_init(&cache,entries,256);
    assert(cache.sets==128 && cache.ways==2);
    for(unsigned pass=0;pass<2;++pass)for(unsigned f=0;f<STREAM_FRAMES;++f) {
        expect(&stream,f,&table,want,expected,&expected_mask);
        assert(LOCAL(&stream,f,&cache) && SAME());
    }
    assert(cache.misses==STREAM_FRAMES && cache.hits==STREAM_FRAMES && !cache.evictions);
    /* A capacity that is not a power of two is one set with rotating ways. */
    t3_psx_local_pose_cache_init(&cache,entries,3);
    assert(cache.sets==1 && cache.ways==3);
    for(unsigned f=0;f<8;++f) {
        expect(&stream,f,&table,want,expected,&expected_mask);
        assert(LOCAL(&stream,f,&cache) && SAME());
    }
    assert(cache.misses==8 && cache.evictions==5);
#undef SAME
#undef LOCAL
}
int main(void) {
    end_to_end();
    failure_contract();
    local_pose_cache();
    puts("PS1 four-limb integer IK / 18-slot matrix completion: passed");
    return 0;
}
