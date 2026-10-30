#ifndef T3_PSX_FLIPBOOK_H
#define T3_PSX_FLIPBOOK_H
/* The original's flipbook ring (EffectSpawn FUN_80076B6C, three 64-byte records at
 * USA 0x800A3408): hit bursts and character flipbooks. State only; drawing is the
 * caller's. Rules read from the decompilation and checked against a PS1 trace of the
 * three records on 23 consecutive fight frames (tests/test_psx_flipbook.c). */
#include <stdint.h>
enum { T3_PSX_FLIPBOOK_RECORDS=3 };
typedef struct {
    uint8_t active,set,index;
    uint16_t frames,frame;     /* frame counts from 1; the record ends once it passes `frames` */
    int32_t pos[3];            /* world position << 7, as stored by the original */
    int32_t vel[3];            /* per frame, same scale */
} t3_psx_flipbook_t;
typedef struct {
    t3_psx_flipbook_t record[T3_PSX_FLIPBOOK_RECORDS];
    uint8_t cursor;
    int32_t last_point[3];     /* diagnostic: the last spawn's point and direction vector */
    int16_t last_vector[3];
} t3_psx_flipbook_pool_t;
void t3_psx_flipbook_clear(t3_psx_flipbook_pool_t* pool);
/* EffectSpawn: take the next record, position = point << 7, and unless `no_velocity`
 * the velocity is `vector * 0x1900 / ISqrt(|vector|^2)` per component (zero for a zero
 * vector). `frames` is the set's frame count. */
void t3_psx_flipbook_spawn(t3_psx_flipbook_pool_t* pool,unsigned set,unsigned index,unsigned frames,
                           const int32_t point[3],const int16_t vector[3],int no_velocity);
/* One game frame after the spawns: frame + 1, velocity -= velocity * 5 / 64 (C division),
 * position += velocity; records past their last frame end. */
void t3_psx_flipbook_step(t3_psx_flipbook_pool_t* pool);
#endif
