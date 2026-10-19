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
int t3_psx_vector_normal(const int32_t v[3],const t3_psx_rsqrt_t* table,int32_t out[3]) {
    int32_t c[3],r[3],sq,norm,coefficient;
    unsigned lz=0,shift;
    if(!v || !table || !table->data || table->size!=T3_PSX_RSQRT_BYTES || !out)return 0;
    for(unsigned i=0;i<3;++i)c[i]=s16(v[i]);
    sq=s32((int64_t)c[0]*c[0]+(int64_t)c[1]*c[1]+(int64_t)c[2]*c[2]);
    if(sq==0){out[0]=out[1]=out[2]=0;return 1;}
    if(sq>0) {
        uint32_t bits=(uint32_t)sq;
        while(!(bits&0x80000000u)){++lz;bits<<=1;}
        lz&=0x1Eu;
    }
    shift=(31u-lz)>>1;
    norm=lz>=24u?s32((int64_t)sq*((int64_t)1<<(lz-24u))):sat_floor_shift_i32(sq,24u-lz);
    /* Nonzero positive sq yields 64..255; wrapped-negative sq -128..-1. */
    if(norm< -128 || norm>255)return 0;
    {
        unsigned at=(unsigned)(norm+128)*2u;
        coefficient=s16((int32_t)table->data[at]|((int32_t)table->data[at+1u]<<8));
    }
    for(unsigned i=0;i<3;++i)r[i]=sat_floor_shift_i32(s32((int64_t)coefficient*c[i]),shift);
    for(unsigned i=0;i<3;++i)out[i]=r[i];
    return 1;
}
int t3_psx_matrix_orthonormalize(const t3_psx_matrix_t* matrix,
                               const t3_psx_rsqrt_t* table,t3_psx_matrix_t* out) {
    int32_t v[3],a[3],b[3],dot;
    t3_psx_matrix_t result;
    if(!matrix || !out)return 0;
    for(unsigned i=0;i<3;++i)v[i]=matrix->m[i][0];
    if(!t3_psx_vector_normal(v,table,a))return 0;
    dot=sat_floor_shift_i32(s32((int64_t)matrix->m[0][1]*a[0]+
        (int64_t)matrix->m[1][1]*a[1]+(int64_t)matrix->m[2][1]*a[2]),12);
    for(unsigned i=0;i<3;++i)
        v[i]=s32((int64_t)matrix->m[i][1]-sat_floor_shift_i32(s32((int64_t)dot*a[i]),12));
    if(!t3_psx_vector_normal(v,table,b))return 0;
    for(unsigned i=0;i<3;++i) {
        unsigned j=(i+1u)%3u,k=(i+2u)%3u;
        result.m[i][0]=s16(a[i]);result.m[i][1]=s16(b[i]);
        result.m[i][2]=s16(sat_floor_shift_i64((int64_t)s16(a[j])*s16(b[k])-
                                                     (int64_t)s16(a[k])*s16(b[j]),12));
    }
    *out=result;return 1;
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
    *out=result;return 1;
}
