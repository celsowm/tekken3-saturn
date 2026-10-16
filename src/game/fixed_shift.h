#ifndef T3_FIXED_SHIFT_H
#define T3_FIXED_SHIFT_H
#include <stdint.h>
/* Floor division by 2^bits (0..62), followed by the caller's int32 narrowing.
 * GCC/Clang targets used here define signed right shift as arithmetic. Keep
 * a portable branch for compilers with a logical signed shift. In particular,
 * do not express this as signed division on SH-2: that introduces unnecessary
 * sign correction and multiword arithmetic for every pose matrix element. */
static inline int32_t t3_floor_shift(int64_t value,unsigned bits) {
#if (-1 >> 1) == -1
    return (int32_t)(value >> bits);
#else
    int64_t divisor=(int64_t)1<<bits;
    return (int32_t)(value/divisor-(value<0 && value%divisor!=0));
#endif
}
#endif
