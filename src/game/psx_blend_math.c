/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Adapted from pinned Tekken3Remake tools/research/fight_sim.py VectorNormal,
 * MatrixOrthonormalize and MatrixAddScaled. Changes: caller-owned LE tables,
 * explicit portable wrapping/rounding and atomic validation, no global RAM.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "psx_blend_math.h"
#include <saturn/int_math.h>

static int16_t s16(int32_t x) {
    uint16_t u=(uint16_t)x;
    return (int16_t)(u<32768u?(int32_t)u:(int32_t)u-65536);
}
static int32_t s32(int64_t x) {
    uint32_t u=(uint32_t)x;
    return u<0x80000000u?(int32_t)u:(int32_t)((int64_t)u-4294967296LL);
}
/* floor(x/2^s) for 0<=s<=15. SH-2 has no barrel shifter, so a variable
 * arithmetic shift is a libgcc loop; the high word of x*2^(32-s) is the same
 * floor quotient and costs one dmuls. */
static int32_t floor_shift_small(int32_t x,unsigned s) {
    static const int32_t scale[16]={0,0,1<<30,1<<29,1<<28,1<<27,1<<26,1<<25,
        1<<24,1<<23,1<<22,1<<21,1<<20,1<<19,1<<18,1<<17};
    if(s<2u)return s?sat_floor_shift_i32(x,1u):x;
    return (int32_t)((uint64_t)((int64_t)x*scale[s])>>32);
}
int t3_psx_vector_normal(const int32_t v[3],const t3_psx_rsqrt_t* table,int32_t out[3]) {
    int32_t c[3],sq,norm,coefficient;
    unsigned lz=0,shift;
    if(!v || !table || !table->data || table->size!=T3_PSX_RSQRT_BYTES || !out)return 0;
    for(unsigned i=0;i<3;++i)c[i]=s16(v[i]);
    sq=s32((int64_t)c[0]*c[0]+(int64_t)c[1]*c[1]+(int64_t)c[2]*c[2]);
    if(sq==0){out[0]=out[1]=out[2]=0;return 1;}
    if(sq>0) {
        /* Even part of the leading-zero count. Then bits==sq*2^lz exactly,
         * so floor(sq*2^(lz-24)) (wrapping for lz>24) is bits>>24. */
        uint32_t bits=(uint32_t)sq;
        if(bits<0x10000u){lz+=16u;bits<<=16;}
        if(bits<0x1000000u){lz+=8u;bits<<=8;}
        if(bits<0x10000000u){lz+=4u;bits<<=4;}
        if(bits<0x40000000u){lz+=2u;bits<<=2;}
        norm=(int32_t)(bits>>24);
    } else norm=sat_floor_shift_i32(sq,24u);
    shift=(31u-lz)>>1;
    /* Nonzero positive sq yields 64..255; wrapped-negative sq -128..-1. */
    {
        unsigned at=(unsigned)(norm+128)*2u;
        coefficient=s16((int32_t)table->data[at]|((int32_t)table->data[at+1u]<<8));
    }
    /* |coefficient*c| <= 2^30, so the product needs no wrapping. */
    for(unsigned i=0;i<3;++i)out[i]=floor_shift_small(coefficient*c[i],shift);
    return 1;
}
/* Low 32 bits of a signed product, i.e. s32 of the exact 64-bit product. */
static int32_t wrap_mul(int32_t x,int32_t y) {return (int32_t)((uint32_t)x*(uint32_t)y);}
/* s16(x)*s16(y)-s16(z)*s16(w) is within (-2^31,2^31), so the 12-bit floor
 * shift of the exact 64-bit difference can be taken in 32 bits. */
static int16_t cross12(int32_t x,int32_t y,int32_t z,int32_t w) {
    return s16(sat_floor_shift_i32((int32_t)s16(x)*s16(y)-(int32_t)s16(z)*s16(w),12u));
}
int t3_psx_matrix_orthonormalize(const t3_psx_matrix_t* matrix,
                               const t3_psx_rsqrt_t* table,t3_psx_matrix_t* out) {
    int32_t v[3],a[3],b[3],dot;
    t3_psx_matrix_t result;
    if(!matrix || !out)return 0;
    for(unsigned i=0;i<3;++i)v[i]=matrix->m[i][0];
    if(!t3_psx_vector_normal(v,table,a))return 0;
    /* Every s32() of a sum or difference of exact products is the same
     * value when computed with wrapping 32-bit arithmetic. */
    dot=sat_floor_shift_i32((int32_t)((uint32_t)wrap_mul(matrix->m[0][1],a[0])+
        (uint32_t)wrap_mul(matrix->m[1][1],a[1])+(uint32_t)wrap_mul(matrix->m[2][1],a[2])),12u);
    for(unsigned i=0;i<3;++i)
        v[i]=(int32_t)((uint32_t)matrix->m[i][1]-
            (uint32_t)sat_floor_shift_i32(wrap_mul(dot,a[i]),12u));
    if(!t3_psx_vector_normal(v,table,b))return 0;
    for(unsigned i=0;i<3;++i){result.m[i][0]=s16(a[i]);result.m[i][1]=s16(b[i]);}
    result.m[0][2]=cross12(a[1],b[2],a[2],b[1]);
    result.m[1][2]=cross12(a[2],b[0],a[0],b[2]);
    result.m[2][2]=cross12(a[0],b[1],a[1],b[0]);
    t3_psx_matrix_copy(out,&result);return 1;
}
int t3_psx_matrix_add_scaled(const t3_psx_matrix_t* delta,const t3_psx_matrix_t* base,
                            int16_t weight,t3_psx_matrix_t* out) {
    t3_psx_matrix_t result;
    if(!delta || !base || !out)return 0;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        int32_t v=sat_floor_shift_i32((int32_t)weight*delta->m[i][j],12);
        if(v>32767)v=32767;
        if(v< -32768)v=-32768;
        result.m[i][j]=s16(v+base->m[i][j]);
    }
    t3_psx_matrix_copy(out,&result);return 1;
}
