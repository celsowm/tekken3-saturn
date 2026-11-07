#ifndef T3_SH2_DIVIDE_H
#define T3_SH2_DIVIDE_H
#include <stdint.h>

/* n / d truncated toward zero, as C, for d != 0 and not INT32_MIN / -1 (the
 * caller keeps those on its own path). On the SH-2 the on-chip divider: the
 * sign-extended 64/32 form (DVSR, DVDNTH, then writing DVDNTL starts it),
 * 39 cycles against about 80 for libgcc's __sdivsi3. Each CPU has its own
 * divider; only explicit code uses it (this, LibSaturn's math and skinning,
 * the panorama), none of it from an interrupt. */
static inline int32_t t3_div32(int32_t n,int32_t d) {
#if defined(__sh__)
    *(volatile uint32_t*)0xFFFFFF00u=(uint32_t)d;
    *(volatile uint32_t*)0xFFFFFF10u=(uint32_t)(n>>31);
    *(volatile uint32_t*)0xFFFFFF14u=(uint32_t)n;
    return (int32_t)*(volatile uint32_t*)0xFFFFFF14u;
#else
    return n/d;
#endif
}

#endif
