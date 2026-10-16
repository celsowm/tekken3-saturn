#include "psx_blend_math.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

int main(void) {
    uint8_t raw[T3_PSX_RSQRT_BYTES]={0};
    t3_psx_rsqrt_t table={raw,sizeof(raw)};
    t3_psx_matrix_t identity={{{4096,0,0},{0,4096,0},{0,0,4096}}},m,out;
    int32_t v[3]={4096,0,0},n[3]={1,2,3};
    /* Length 4096: normalized index64, arbitrary coefficient4095 to exercise
     * exact lookup/rounding. No retail lookup coefficients in fixture. */
    raw[384]=255;raw[385]=15;
    assert(t3_psx_vector_normal(v,&table,n));
    assert(n[0]==4095 && n[1]==0 && n[2]==0);
    table.size--;
    assert(!t3_psx_vector_normal(v,&table,n));
    assert(n[0]==4095);
    m=identity;
    assert(!t3_psx_matrix_orthonormalize(&m,&table,&m));
    assert(memcmp(&m,&identity,sizeof(m))==0);
    table.size++;
    assert(t3_psx_matrix_orthonormalize(&m,&table,&out));
    assert(t3_psx_matrix_orthonormalize(&m,&table,&m));
    assert(memcmp(&m,&out,sizeof(m))==0);
    assert(t3_psx_vector_normal(v,&table,v));assert(v[0]==4095);
    v[0]=v[1]=v[2]=0;
    assert(t3_psx_vector_normal(v,&table,v));assert(!v[0] && !v[1] && !v[2]);
    m=identity;
    assert(t3_psx_matrix_add_scaled(&identity,&m,4096,&m));assert(m.m[0][0]==8192);
    m.m[0][0]=32767;
    assert(t3_psx_matrix_add_scaled(&m,&m,32767,&m));assert(m.m[0][0]==-2);
    assert(!t3_psx_matrix_add_scaled(0,&m,0,&m));
    assert(!t3_psx_matrix_orthonormalize(0,&table,&m));
    assert(!t3_psx_vector_normal(0,&table,v));
    return 0;
}
