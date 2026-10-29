/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Tekken3Remake research describes the source texture and CLUT conventions.
 * See NOTICE.md. */
#ifndef T3_PSX_FACE_COVERAGE_H
#define T3_PSX_FACE_COVERAGE_H
#include <stdint.h>
/* Convert an 8x8 indexed texture patch into an 8-row opacity mask (bit 7
 * is the leftmost pixel). `zero_is_opaque` describes the actual VDP1 draw
 * command's SPD bit, not the model asset's nominal texture flag. This only
 * describes texel opacity; it does not map the patch to screen pixels. */
int t3_face_index8_coverage(const uint8_t pixels[64],int zero_is_opaque,uint8_t mask[8]);
/* Experimental axis-aligned VDP1 sprite mask for one screen tile. Coordinates
 * are inclusive and expressed in screen pixels. Only non-flipped rectangles
 * are supported; malformed rectangles fail without modifying mask. This is
 * not a general distorted-sprite rasterizer. */
int t3_face_rect_tile_coverage(const uint8_t pixels[64],int zero_is_opaque,
    int left,int top,int right,int bottom,unsigned tile_x,unsigned tile_y,uint8_t mask[8]);
/* Experimental triangle expressed in VDP1 A,B,C,D order (C == D). Accepts
 * horizontal top edges and sides whose horizontal displacement does not
 * exceed their vertical span. Other shapes fail without modifying mask. */
int t3_face_triangle_tile_coverage(const uint8_t pixels[64],int zero_is_opaque,
    const int16_t x[4],const int16_t y[4],unsigned tile_x,unsigned tile_y,uint8_t mask[8]);
/* Experimental parallelogram coverage; nonnegative line/edge Y directions,
 * horizontal/vertical major axes and at most 64 pixels per edge. Other
 * shapes fail atomically. */
int t3_face_parallelogram_tile_coverage(const uint8_t pixels[64],int zero_is_opaque,
    const int16_t x[4],const int16_t y[4],unsigned tile_x,unsigned tile_y,uint8_t mask[8]);
/* Experimental forward raster for bounded convex screen quads with downward
 * vertical-major sides and rightward horizontal-major spans. Unsupported
 * geometry fails atomically. Rotated C=D triangles are excluded. Does not
 * establish physical-hardware parity. */
int t3_face_quad_tile_coverage(const uint8_t pixels[64],int zero_is_opaque,
    const int16_t x[4],const int16_t y[4],unsigned tile_x,unsigned tile_y,uint8_t mask[8]);
/* Geometry-only preflight for the same bounded quad path. */
int t3_face_quad_supported(const int16_t x[4],const int16_t y[4]);
/* Experimental bounded, arbitrary-direction distorted sprite. This is not
 * yet enabled in the particle compositor or validated on physical hardware. */
int t3_face_general_tile_coverage(const uint8_t pixels[64],int zero_is_opaque,
    const int16_t x[4],const int16_t y[4],unsigned tile_x,unsigned tile_y,uint8_t mask[8]);
#endif
