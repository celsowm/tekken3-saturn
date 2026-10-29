#ifndef T3_STAGE_FLOOR_BITMAP_H
#define T3_STAGE_FLOOR_BITMAP_H
#include <stdint.h>
/* Expand the existing 64 8x8 LUT4 cells into one row of a 512x256 LUT4
 * bitmap. Both representations repeat the same 64x64 tile. Bytes contain
 * the leftmost pixel in the high nibble, as required by VDP2. */
int t3_stage_floor_bitmap_row(const uint8_t cells[2048],unsigned y,uint8_t row[256]);
#endif
