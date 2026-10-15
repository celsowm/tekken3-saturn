#include "psx_ik.h"
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
int main(void) {
    end_to_end();
    failure_contract();
    puts("PS1 four-limb integer IK / 18-slot matrix completion: passed");
    return 0;
}
