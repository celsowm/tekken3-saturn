#include "psx_trace.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint32_t r32(const uint8_t* p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|
           ((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);
}
static int16_t r16(const uint8_t* p) {
    uint16_t v=(uint16_t)(p[0]|((uint16_t)p[1]<<8u));
    return (int16_t)(v<0x8000u?v:(int32_t)v-65536);
}
static void bytes_and_masks(void) {
    uint8_t out[T3_PSX_TRACE_BYTES];
    t3_psx_joint_t joints[T3_PSX_DRAW_PARTS]={0};
    unsigned i;
    memset(out,0xEE,sizeof(out));
    joints[0].origin=(t3_vec3i_t){123,-456,789};
    joints[0].rotation.m[0][0]=4096;
    joints[0].rotation.m[0][1]=-2048;
    joints[0].rotation.m[2][2]=4096;
    joints[1].origin.x=987654;
    assert(t3_psx_trace_encode(0x12345678u,0u,333u,15u,joints,
                               1u,out,sizeof(out)));
    assert(out[0]=='T' && out[1]=='3' && out[2]=='P' && out[3]=='X');
    assert(out[4]==1 && out[5]==0);
    assert(out[6]==18 && out[7]==0);
    assert(r32(out+8)==0x12345678u);
    assert(out[12]==(333u&255u) && out[13]==(333u>>8u));
    assert(out[14]==15 && out[16]==0);
    assert(r32(out+20)==1u);
    assert((int32_t)r32(out+24)==123);
    assert((int32_t)r32(out+28)==-456);
    assert((int32_t)r32(out+32)==789);
    assert(r16(out+36)==4096);
    assert(r16(out+38)==-2048);
    assert(r16(out+24+12+8*2)==4096);
    for(i=24u+30u;i<sizeof(out);++i)assert(out[i]==0);
    assert(t3_psx_trace_encode(987u,1u,3u,6u,NULL,0u,out,sizeof(out)));
    assert(r32(out+20)==0u && out[16]==1);
    for(i=24u;i<sizeof(out);++i)assert(out[i]==0);
}
static void atomic_error(void) {
    uint8_t out[T3_PSX_TRACE_BYTES],saved[T3_PSX_TRACE_BYTES];
    t3_psx_joint_t joints[T3_PSX_DRAW_PARTS]={0};
    memset(out,0xA5,sizeof(out));
    memcpy(saved,out,sizeof(out));
    assert(!t3_psx_trace_encode(1,0,0,0,joints,0u,out,10u));
    assert(!memcmp(out,saved,sizeof(out)));
    assert(!t3_psx_trace_encode(1,2,0,0,joints,0u,out,sizeof(out)));
    assert(!memcmp(out,saved,sizeof(out)));
    assert(!t3_psx_trace_encode(1,0,0,0,joints,1u,NULL,sizeof(out)));
    assert(!t3_psx_trace_encode(1,0,0,0,NULL,1u,out,sizeof(out)));
    assert(!memcmp(out,saved,sizeof(out)));
    assert(!t3_psx_trace_encode(1,0,0,0,joints,1u<<18,out,sizeof(out)));
    assert(!memcmp(out,saved,sizeof(out)));
    joints[0].origin.x=T3_PSX_UNIT_LIMIT+1;
    assert(!t3_psx_trace_encode(1,0,0,0,joints,1u,out,sizeof(out)));
    assert(!memcmp(out,saved,sizeof(out)));
}
int main(void) {
    bytes_and_masks();
    atomic_error();
    puts("PS1 oracle pose trace: stable LE encoding and atomic failure passed");
    return 0;
}
