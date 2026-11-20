#ifndef T3_STAGE_FORMAT_H
#define T3_STAGE_FORMAT_H
/* The layout of STGnn.BIN (scripts/build_stage_packs.py reads these defines).
 * Big-endian, whole sectors: a head of 16 words, the floor palette and the
 * panorama palette, then the floor bitmap and the panorama picture.
 *
 * Version 3 (Tekken Ball's court, stage 19): the picture is a COLUMNS x ROWS
 * 8-bit bitmap with 255 colours. Version 4 (the fight stages): the picture is
 * PANO_COLUMNS x PANO_ROWS VDP2 cells, 4 bits a dot, with 16 palettes of 15
 * colours (entry 0 is transparent): the 2-word pattern names of the cell grid
 * (palette number T3_STAGE_PALETTE_NUMBER + palette, character number), then
 * the characters, 32 bytes each, character 0 being the transparent one. */
#define T3_STAGE_MAGIC 0x54335354u
#define T3_STAGE_VERSION 3u
#define T3_STAGE_CELL_VERSION 4u
#define T3_STAGE_COLUMNS 1024u
#define T3_STAGE_ROWS 240u
#define T3_STAGE_FLOOR_W 512u
#define T3_STAGE_FLOOR_H 256u
#define T3_STAGE_BAKE_HEIGHT 1100
/* Q16: the bake's elevation tangent of row 0, and rows per unit of tangent. */
#define T3_STAGE_TAN_TOP 52429
#define T3_STAGE_ROWS_PER_TAN 13107200
#define T3_STAGE_PANO_COLUMNS 2048u
#define T3_STAGE_PANO_ROWS 352u
/* Q16, 293 rows to the tangent: the same 1.2 of tangent as the 240 rows above. */
#define T3_STAGE_PANO_ROWS_PER_TAN 19202048
/* Colour RAM entry 1792 (bank 7) is palette number 112 in 16-colour cells. */
#define T3_STAGE_PALETTE_NUMBER 112u
/* The characters fill VRAM A0/A1 below the line and coefficient tables. */
#define T3_STAGE_MAX_CHARS 7680u
#define T3_STAGE_HEAD_BYTES 2048u
#define T3_STAGE_FLOOR_PALETTE_AT 512u
#define T3_STAGE_PICTURE_PALETTE_AT 1024u
#define T3_STAGE_FLOOR_AT T3_STAGE_HEAD_BYTES
#define T3_STAGE_FLOOR_BYTES (T3_STAGE_FLOOR_W*T3_STAGE_FLOOR_H)
#define T3_STAGE_PICTURE_AT (T3_STAGE_FLOOR_AT+T3_STAGE_FLOOR_BYTES)
#define T3_STAGE_PICTURE_BYTES (T3_STAGE_COLUMNS*T3_STAGE_ROWS)
#define T3_STAGE_FILE_BYTES (T3_STAGE_PICTURE_AT+T3_STAGE_PICTURE_BYTES)
#define T3_STAGE_NAMES_AT T3_STAGE_PICTURE_AT
#define T3_STAGE_NAMES_BYTES (T3_STAGE_PANO_COLUMNS/8u*(T3_STAGE_PANO_ROWS/8u)*4u)
#define T3_STAGE_CHARS_AT (T3_STAGE_NAMES_AT+T3_STAGE_NAMES_BYTES)
enum {
    T3_STAGE_WORD_MAGIC, T3_STAGE_WORD_VERSION, T3_STAGE_WORD_NUMBER,
    T3_STAGE_WORD_BOX_HALF,      /* panorama box half-width, PS1 units */
    T3_STAGE_WORD_BACK,          /* clear colour, RGB555 */
    T3_STAGE_WORD_SHADOW,        /* drop shadow, RGB555 */
    T3_STAGE_WORD_OGRE_LEVEL,    /* floor brightness in a True Ogre fight, 128 unchanged */
    T3_STAGE_WORD_CHARS,         /* version 4: the number of characters */
    T3_STAGE_WORDS=16
};
#endif
