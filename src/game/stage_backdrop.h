#ifndef T3_STAGE_BACKDROP_H
#define T3_STAGE_BACKDROP_H
#include <stdint.h>

/* The stage panorama as one 360-degree cylinder picture, drawn on a scrolled
 * layer that follows the camera line by line.
 *
 * The original draws the panorama box scaled by 8 around the fighters' root
 * midpoint, lowered by a per-stage height and turned by Ry(-turn)
 * (StageBackgroundDraw, Tekken3Remake docs/research/code/stages.md). The
 * picture is baked from one point above the midpoint: column c looks along
 * yaw -c * 4096 / columns (PS1 yaw units, so columns grow to the screen's
 * right) and row r at the elevation tangent tan_top - r / rows_per_tan.
 *
 * On every 32nd screen line, five rays from the real camera, spread across
 * the screen, are followed into the box walls, and each is mapped to the
 * picture position the bake point sees there. The line's scroll and
 * increment are the least-squares fit of those columns and its row their
 * mean; the lines between are interpolated, so parallax, the turn and the
 * pitch are followed down the screen. A line can only scroll, so a
 * box corner on screen or a camera far above or below the bake point bends
 * the picture by a few texels (scripts/check_backdrop_mapping.py). */

typedef struct {
    int32_t box_half;        /* PS1 units from the midpoint to the walls (x and z) */
    int32_t bake_height;     /* PS1 units of the bake point above the floor */
    int32_t tan_top;         /* elevation tangent of row 0, 16.16 */
    int32_t rows_per_tan;    /* rows per unit of tangent, 16.16 */
    uint16_t columns, rows;  /* picture size: columns wrap, rows clamp */
    uint16_t width, height;  /* screen size in dots */
} t3_stage_backdrop_t;

/* One line: the layer's horizontal scroll (picture column of screen dot 0),
 * vertical scroll (picture row of the group's first line: the VDP2 replaces
 * the layer's vertical coordinate with it and adds 1 a line) and horizontal
 * increment, all 16.16, as a VDP2 line scroll entry stores them. */
typedef struct { int32_t x,y; uint32_t zoom; } t3_stage_backdrop_line_t;

/* `view_proj` (16 entries, 16.16, row-major, GL convention: row 0 is
 * fx * right, row 1 fy * up, row 3 the forward axis) and `eye` (16.16) are
 * the Saturn camera, whose world is 96 fx16 per PS1 unit around the
 * midpoint with axes x, -y, -z. `turn` is the panorama's turn (4096 a turn).
 * Fills `height / step` lines (one per `step` screen lines, 1, 2, 4 or 8) and,
 * when `wall_depth` is not NULL, each line's depth to the wall along the
 * forward axis (16.16 Saturn units, 0 when the line sees no wall). Returns 0
 * for invalid arguments or a camera that rolls. */
int t3_stage_backdrop_lines(const t3_stage_backdrop_t* backdrop,const int32_t view_proj[16],
                            const int32_t eye[3],int32_t turn,unsigned step,
                            t3_stage_backdrop_line_t* lines,int32_t* wall_depth);
#endif
