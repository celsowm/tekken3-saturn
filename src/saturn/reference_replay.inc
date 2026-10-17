/* Opt-in private diagnostic: one original seed, then external IO only.
 * The same native match runs underneath the normal renderer. No expected
 * snapshots/digests are linked into this executable. */
#include "t3_reference_fixture.h"
#include "psx_ground_eval_fields.h"
#include "psx_round_flow_eval_fields.h"
#include "sha256.h"
volatile uint32_t g_reference_count,g_reference_error;
/* Artifact identity, not per-tick expected state. The host binds the observed
 * trace to this exact exported seed/input/schema manifest. */
volatile const uint8_t g_reference_fixture_id[32]=T3_REFERENCE_FIXTURE_ID;
volatile uint8_t g_reference_trace[T3_REFERENCE_TICKS][32]
    __attribute__((section(".wram_l")));
volatile uint16_t g_reference_value_counts[T3_REFERENCE_TICKS]
    __attribute__((section(".wram_l")));
static struct {uint32_t address,size,offset;} g_reference_ranges[64]
    __attribute__((section(".wram_l")));
static unsigned g_reference_range_count,g_reference_cursor;
static uint32_t g_reference_source_frame;
static t3_sha256_t reference_digest __attribute__((section(".wram_l")));
static uint32_t reference_value_count;
static int bad;
static uint32_t reference_le32(const uint8_t* p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static const uint8_t* at(uint32_t address,unsigned size) {
    for(unsigned i=0;i<g_reference_range_count;++i) {
        uint32_t a=g_reference_ranges[i].address,n=g_reference_ranges[i].size;
        if(address>=a && size<=n && address-a<=n-size)
            return t3_reference_seed+g_reference_ranges[i].offset+address-a;
    }
    bad=1;return t3_reference_seed;
}
static uint8_t u8(uint32_t address){return *at(address,1);}
static uint16_t u16(uint32_t address){const uint8_t* p=at(address,2);return (uint16_t)(p[0]|p[1]<<8);}
static int16_t i16(uint32_t address){uint16_t n=u16(address);return (int16_t)(n<32768?n:(int32_t)n-65536);}
static uint32_t u32(uint32_t address){return reference_le32(at(address,4));}
static int32_t i32(uint32_t address){uint32_t n=u32(address);return n<0x80000000u?(int32_t)n:(int32_t)((int64_t)n-4294967296LL);}
static void reference_value(int64_t value) {
    uint64_t bits=(uint64_t)value;uint8_t bytes[8];
    for(unsigned i=0;i<8;++i)bytes[i]=(uint8_t)(bits>>(56-8*i));
    t3_sha256_update(&reference_digest,bytes,8);++reference_value_count;
}
#define original_round_flow 1
#define game g_owned_match
#define config g_owned_config
#define bank g_bank
/* Expected expressions are discarded by macro expansion: no exit RAM reads.
 * Field names are metadata on the host; canonical scalar order is shared. */
#define check(i,category,field,actual,expected) ((void)(i),(void)(field),reference_value((int64_t)(actual)))
#define REFERENCE_NAME(buffer,...) ((void)(buffer))
#include "psx_match_reference.inc"
#undef REFERENCE_NAME
#undef check
#undef bank
#undef config
#undef game
#undef original_round_flow
static int reference_prepare_config(void) {
    (void)g_reference_fixture_id[0]; /* Retain the debugger-visible identity. */
    bad=0;g_reference_count=g_reference_error=g_reference_cursor=0;
    g_reference_source_frame=T3_REFERENCE_FIRST_FRAME;
    if(sizeof(t3_reference_seed)<12)return 0;
    g_reference_range_count=reference_le32(t3_reference_seed+8);
    if(!g_reference_range_count || g_reference_range_count>64)return 0;
    uint32_t offset=12;
    for(unsigned i=0;i<g_reference_range_count;++i) {
        if(offset>sizeof(t3_reference_seed)-8)return 0;
        uint32_t address=reference_le32(t3_reference_seed+offset);
        uint32_t size=reference_le32(t3_reference_seed+offset+4);offset+=8;
        if(!size || address<0x80000000u || size>0x200000u ||
           address-0x80000000u>0x200000u-size || size>sizeof(t3_reference_seed)-offset)return 0;
        g_reference_ranges[i].address=address;g_reference_ranges[i].size=size;
        g_reference_ranges[i].offset=offset;offset+=size;
    }
    if(offset!=sizeof(t3_reference_seed))return 0;
    g_owned_config.max_health_q16=i32(0x800A9620u);
    g_owned_config.collapse_on_ko=u8(0x800AFA93u);
    g_owned_config.chip_enabled=u32(0x800AFA58u)!=0;
    g_owned_config.spawn[0]=(t3_psx_spawn_t){0};
    g_owned_config.spawn[1]=(t3_psx_spawn_t){0};
    return !bad;
}
static int reference_input(uint32_t tick,t3_input_t* first,t3_input_t* second,
                            t3_psx_match_frame_t* frame) {
    if(tick>=T3_REFERENCE_TICKS)return 0;
    if(tick) {
        unsigned shift=2*((tick-1)%4);
        unsigned code=(t3_reference_deltas[(tick-1)/4]>>shift)&3u;
        g_reference_source_frame+=code==3?4:code;
    }
    while(g_reference_cursor+1<T3_REFERENCE_EVENTS &&
          t3_reference_events[g_reference_cursor+1].tick<=tick)++g_reference_cursor;
    const uint16_t* w=t3_reference_events[g_reference_cursor].words;
    *first=(t3_input_t){w[0],w[1]};*second=(t3_input_t){w[2],w[3]};
    *frame=(t3_psx_match_frame_t){g_reference_source_frame,{w[4],w[5]},{w[6],w[7]}};
    return 1;
}
static int reference_observe(void) {
    uint8_t digest[32];uint32_t tick=g_owned_match.ticks;
    if(!tick || tick>T3_REFERENCE_TICKS || g_reference_count!=tick-1)return 0;
    t3_sha256_init(&reference_digest);reference_value_count=0;
    compare(0,T3_REFERENCE_STEP_X,T3_REFERENCE_STEP_Z);
    compare(1,T3_REFERENCE_STEP_X,T3_REFERENCE_STEP_Z);compare_round();
    t3_sha256_finish(&reference_digest,digest);
    for(unsigned i=0;i<32;++i)g_reference_trace[tick-1][i]=digest[i];
    if(reference_value_count>65535)return 0;
    g_reference_value_counts[tick-1]=(uint16_t)reference_value_count;
    g_reference_count=tick;return 1;
}
