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
#if defined(T3_REFERENCE_TRACE_HIGH)
#define T3_REFERENCE_TRACE_SECTION
#else
#define T3_REFERENCE_TRACE_SECTION __attribute__((section(".wram_l")))
#endif
volatile uint8_t g_reference_trace[T3_REFERENCE_TICKS][32]
    T3_REFERENCE_TRACE_SECTION;
volatile uint16_t g_reference_value_counts[T3_REFERENCE_TICKS]
    __attribute__((section(".wram_l")));
#undef T3_REFERENCE_TRACE_SECTION
static struct {uint32_t address,size,offset;} g_reference_ranges[64]
    __attribute__((section(".wram_l")));
static unsigned g_reference_range_count,g_reference_cursor;
static uint32_t g_reference_source_frame;
static t3_sha256_t reference_digest __attribute__((section(".wram_l")));
static uint32_t reference_value_count;
#if defined(T3_REFERENCE_DEBUG_VALUES)
volatile int64_t g_reference_first_values[1400] __attribute__((section(".wram_l")));
volatile uint8_t g_reference_debug_history[2*sizeof(t3_input_history_t)]
    __attribute__((section(".wram_l")));
volatile uint32_t g_reference_debug_move[2][16] __attribute__((section(".wram_l")));
volatile uint32_t g_reference_debug_pad[8] __attribute__((section(".wram_l")));
volatile uint32_t g_reference_debug_flow[8] __attribute__((section(".wram_l")));
#endif
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
#if defined(T3_REFERENCE_DEBUG_VALUES)
    if(g_reference_count==T3_REFERENCE_DEBUG_TICK && reference_value_count<1400)
        g_reference_first_values[reference_value_count]=value;
#endif
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
    *frame=(t3_psx_match_frame_t){.frame_counter=g_reference_source_frame,
        .physical={w[4],w[5]},.pressed={w[6],w[7]},
        .side_known=T3_REFERENCE_HAS_SIDES,.side={(uint8_t)w[8],(uint8_t)w[9]}};
#if defined(T3_REFERENCE_HAS_VIEW) && T3_REFERENCE_HAS_VIEW
    /* Per-tick screen x the fighter draw left (+0x1278): the external state
     * PairwiseDistances reads. Sparse 38-byte view records additionally carry
     * the previous CameraFrame view for the ticks whose RoundFlow draw uses it. */
    const uint8_t* s=t3_reference_screens+4u*tick;
    frame->screen_known=1;
    frame->screen_x[0]=(int16_t)(uint16_t)(s[0]|(uint16_t)s[1]<<8);
    frame->screen_x[1]=(int16_t)(uint16_t)(s[2]|(uint16_t)s[3]<<8);
    unsigned lo=0,hi=T3_REFERENCE_VIEWS;
    while(lo<hi) {
        unsigned mid=(lo+hi)/2;const uint8_t* r=t3_reference_views+38u*mid;
        uint32_t t=(uint32_t)r[0]|(uint32_t)r[1]<<8|(uint32_t)r[2]<<16|(uint32_t)r[3]<<24;
        if(t<tick)lo=mid+1;else hi=mid;
    }
    if(lo<T3_REFERENCE_VIEWS) {
        const uint8_t* p=t3_reference_views+38u*lo;
        uint32_t t=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        if(t==tick) {
            static t3_psx_camera_matrix_t view;unsigned at=4;
            for(unsigned r=0;r<3;++r)for(unsigned k=0;k<3;++k,at+=2)
                view.m[r][k]=(int16_t)(uint16_t)(p[at]|(uint16_t)p[at+1]<<8);
            for(unsigned k=0;k<3;++k,at+=4)
                view.t[k]=(int32_t)((uint32_t)p[at]|((uint32_t)p[at+1]<<8)|
                    ((uint32_t)p[at+2]<<16)|((uint32_t)p[at+3]<<24));
            view.h=(int32_t)((uint32_t)p[at]|((uint32_t)p[at+1]<<8)|
                ((uint32_t)p[at+2]<<16)|((uint32_t)p[at+3]<<24));
            frame->fighter_view=&view;
        }
    }
#endif
#if defined(T3_REFERENCE_DEBUG_VALUES)
    if(tick==T3_REFERENCE_DEBUG_TICK) {
        const uint32_t values[8]={tick,g_reference_cursor,w[0],w[1],w[2],w[3],w[4],w[5]};
        for(unsigned i=0;i<8;++i)g_reference_debug_pad[i]=values[i];
    }
#endif
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
#if defined(T3_REFERENCE_DEBUG_VALUES)
    if(tick-1==T3_REFERENCE_DEBUG_TICK) {
        const uint32_t flow_words[8]={g_owned_match.flow.input[0],g_owned_match.flow.input[1],
            g_owned_match.round_actor[0].script_input,g_owned_match.round_actor[1].script_input,
            g_owned_match.flow.state,g_owned_match.flow.human_mask,
            g_owned_match.fighter[0].ground.is_cpu,g_owned_match.fighter[1].ground.is_cpu};
        for(unsigned i=0;i<8;++i)g_reference_debug_flow[i]=flow_words[i];
        for(unsigned actor=0;actor<2;++actor) {
            const uint8_t* src=(const uint8_t*)&g_owned_match.fighter[actor].input;
            for(unsigned i=0;i<sizeof(t3_input_history_t);++i)
                g_reference_debug_history[actor*sizeof(t3_input_history_t)+i]=src[i];
            const t3_psx_fighter_t* f=&g_owned_match.fighter[actor];
            const t3_divmot_t* bank=f->vm.bank;
            const uint32_t words[16]={f->vm.slot,f->vm.move.branch_index,
                f->vm.move.animation_offset,f->vm.move.state_word,f->vm.move.flags,
                f->vm.move.length,bank?bank->bank_type:255u,
                bank?bank->branch_count:0u,bank?bank->common_branch_count:0u,
                bank&&bank->linked?bank->linked->bank_type:255u,
                (uint32_t)f->vm.clock.pose_frame,g_owned_config.character[actor],
                f->side_flag,f->ground.distance,f->vm.move.attack_word,
                f->vm.branch_window};
            for(unsigned i=0;i<16;++i)g_reference_debug_move[actor][i]=words[i];
        }
    }
#endif
    g_reference_count=tick;return 1;
}
