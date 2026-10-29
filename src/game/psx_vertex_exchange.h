#ifndef T3_PSX_VERTEX_EXCHANGE_H
#define T3_PSX_VERTEX_EXCHANGE_H
#include <stdint.h>
#include <stddef.h>
enum { T3_PSX_VERTEX_SLOTS=128 };
typedef struct {uint32_t xy[T3_PSX_VERTEX_SLOTS];int16_t z[T3_PSX_VERTEX_SLOTS];} t3_psx_projected_slots_t;
typedef struct {t3_psx_projected_slots_t scratch,global;} t3_psx_vertex_exchange_t;
typedef struct {
    const uint8_t* scratch_import;size_t scratch_count;
    const uint8_t* global_import;size_t global_count;
    const uint32_t* own_xy;const int16_t* own_z;size_t own_count;
    /* 9-bit entries: low byte is exchange offset, bit 8 is halve.
     * Groups: blend global+export, add global, add scratch, export global,
     * export scratch. The sixth group holds two-bit halve codes. */
    const uint16_t* post[6];size_t post_count[6];
} t3_psx_vertex_block_t;
/* Executes one part in original sequential order. State must initially match
 * the desired scratch/global contents. Invalid input leaves state unchanged.
 * Input arrays must not overlap mutable state. Caller computes each own
 * vertex's GTE-projected packed XY and SZ/32. */
int t3_psx_vertex_exchange(t3_psx_vertex_exchange_t* state,const t3_psx_vertex_block_t* block);
#endif
