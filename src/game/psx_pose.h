#ifndef T3_PSX_POSE_H
#define T3_PSX_POSE_H

#include <stddef.h>
#include <stdint.h>
#include "psx_motion.h"
#include "psx_collision.h"

enum {
    T3_PSX_ROTATION_SLOTS = 18,
    T3_PSX_DRAW_PARTS = 22,
    T3_PSX_TRIG_BYTES = 0x2800
};
/* The private PS1 sine table starts at reference address 0x8001E8C4.
 * Both sin(angle) and cos(angle) are read from this same lookup array,
 * with cos shifted by 0x800 bytes (not assuming another region's address).
 * 0x2800 bytes cover both high-angle sine and offset cosine lookups. */
typedef struct {
    const uint8_t* data;
    size_t size;
} t3_psx_trig_t;
typedef struct {
    int16_t m[3][3];  /* 4.12 PS1 rotation, row-major */
} t3_psx_matrix_t;
/* Struct assignment of this 2-aligned, 18-byte type is a memcpy call on the
 * SH-2; nine halfword moves are several times cheaper. */
static inline void t3_psx_matrix_copy(t3_psx_matrix_t* out,const t3_psx_matrix_t* in) {
    out->m[0][0]=in->m[0][0];out->m[0][1]=in->m[0][1];out->m[0][2]=in->m[0][2];
    out->m[1][0]=in->m[1][0];out->m[1][1]=in->m[1][1];out->m[1][2]=in->m[1][2];
    out->m[2][0]=in->m[2][0];out->m[2][1]=in->m[2][1];out->m[2][2]=in->m[2][2];
}
/* 1u << n for n < 32, read from a table: GCC compiles a variable shift, and
 * a test of x & (1u << n), to a libgcc call on the SH-2. */
extern const uint32_t t3_psx_bits[32];
static inline uint32_t t3_psx_bit(unsigned n) {return t3_psx_bits[n];}
/* floor((m . v) / 4096), the 64-bit result, for |v[k]| <= T3_PSX_UNIT_LIMIT.
 * v = high*4096 + low (low 0..4095) keeps every product within 32 bits, so
 * the SH-2 needs no 64-bit multiply. */
static inline int32_t t3_psx_floor12(int32_t v) {
    /* Logical shift, then sign-extend the 20-bit field: the floor quotient
     * without the SH-2's libgcc arithmetic-shift helper. */
    return (int32_t)((((uint32_t)v>>12u)^0x80000u)-0x80000u);
}
#if defined(__sh__)
/* MAC.L keeps the 64-bit sum (S is clear); its low word after the shift. */
static inline int32_t t3_psx_dot12(const int16_t m[3],const int32_t v[3]) {
    const int32_t w[3]={m[0],m[1],m[2]};
    const int32_t* a=w;const int32_t* b=v;
    uint32_t hi,lo;
    __asm__("clrmac\n\tmac.l @%0+,@%1+\n\tmac.l @%0+,@%1+\n\tmac.l @%0+,@%1+\n\t"
            "sts mach,%2\n\tsts macl,%3"
            :"+r"(a),"+r"(b),"=r"(hi),"=r"(lo)
            :"m"(*(const int32_t(*)[3])w),"m"(*(const int32_t(*)[3])v)
            :"mach","macl");
    return (int32_t)(hi<<20|lo>>12);
}
#else
static inline int32_t t3_psx_dot12(const int16_t m[3],const int32_t v[3]) {
    return m[0]*t3_psx_floor12(v[0])+m[1]*t3_psx_floor12(v[1])+m[2]*t3_psx_floor12(v[2])+
        t3_psx_floor12(m[0]*(v[0]&4095)+m[1]*(v[1]&4095)+m[2]*(v[2]&4095));
}
#endif
typedef struct {
    int8_t parent;       /* -1 means character root; otherwise preceding draw part */
    uint8_t slot;        /* PS1 animation/attachment slot 0..21; 18+ pending */
    t3_vec3i_t offset;   /* PS1 model's (x, y, -z), not normalized skeleton */
} t3_psx_part_t;
typedef struct {
    t3_psx_matrix_t rotation;
    t3_vec3i_t origin;
} t3_psx_joint_t;

int t3_psx_euler_matrix(int16_t x,int16_t y,int16_t z,
                        const t3_psx_trig_t* trig,t3_psx_matrix_t* out);

/* Builds ONLY 10 original FK Euler matrices from channels 19..48.
 * Returns a bitmask of VALID slots. IK slots remain invalid; they MUST be
 * supplied by the original limb solver before full character composition.
 * On failure, neither outputs nor mask are modified. */
int t3_psx_pose_fk(const int16_t pose[T3_PSX_POSE_CHANNELS],
                   const t3_psx_trig_t* table,
                   t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],
                   uint32_t* valid_slots);

/* Q12 matrix multiplication, saturating signed 16-bit like the PS1 GTE
 * MVMVA sf=1 lm=0. Can be used for FK joint chains. */
int t3_psx_mul_rot(const t3_psx_matrix_t* a, const t3_psx_matrix_t* b,
                   t3_psx_matrix_t* out);

/* Compose the REAL model's parent/offset table for all 22 draw parts.
 * Missing local IK slots invalidate each affected subtree. No identity
 * fallback is applied. 'root' supplies fighter world position and heading.
 * Returns 1 success or 0 on malformed parents/unsafe coordinates; results
 * are published only on success. */
int t3_psx_compose_parts(const t3_psx_part_t parts[T3_PSX_DRAW_PARTS],
                         const t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],
                         uint32_t valid_slots, const t3_psx_joint_t* root,
                         t3_psx_joint_t out[T3_PSX_DRAW_PARTS],
                         uint32_t* valid_parts);

/* KMD records are mesh order; the original fighter's collision/joint array
 * is animation-slot order. Remap explicitly before using original table indices.
 * Supports in-place use; missing slots are zero and invalid. Failure is atomic. */
int t3_psx_parts_to_slots(const t3_psx_part_t parts[T3_PSX_DRAW_PARTS],
                         const t3_psx_joint_t in[T3_PSX_DRAW_PARTS],uint32_t valid_parts,
                         t3_psx_joint_t out[T3_PSX_DRAW_PARTS],uint32_t* valid_slots);

/* Number of available draw parts, useful for Saturn debug telemetry. */
unsigned t3_psx_count_parts(uint32_t mask);

#endif
