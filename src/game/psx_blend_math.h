#ifndef T3_PSX_BLEND_MATH_H
#define T3_PSX_BLEND_MATH_H
#include "psx_pose.h"

enum { T3_PSX_RSQRT_BYTES = 768 };
/* LE coefficient window: starts 384 bytes BEFORE the reciprocal-sqrt table
 * (reference address 0x8001A194). Negative wrapped squared lengths address
 * that preceding data too. It must come from the same fingerprinted EXE. */
typedef struct { const uint8_t* data; size_t size; } t3_psx_rsqrt_t;

/* Exact signed-16 input / signed-32 intermediate original arithmetic.
 * Failure leaves output untouched. All operations support output aliasing. */
int t3_psx_vector_normal(const int32_t v[3],const t3_psx_rsqrt_t* table,int32_t out[3]);
int t3_psx_matrix_orthonormalize(const t3_psx_matrix_t* matrix,
                               const t3_psx_rsqrt_t* table,t3_psx_matrix_t* out);
int t3_psx_matrix_add_scaled(const t3_psx_matrix_t* delta,const t3_psx_matrix_t* base,
                            int16_t weight,t3_psx_matrix_t* out);
#endif
