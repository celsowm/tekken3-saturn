#ifndef T3_STAGE_FORMAT_H
#define T3_STAGE_FORMAT_H
/* The layout of STGnn.BIN (scripts/build_stage_packs.py reads these defines).
 * Big-endian, whole sectors: a head of 16 words, the floor palette and the
 * panorama palette, then the floor bitmap and the panorama picture. */
#define T3_STAGE_MAGIC 0x54335354u
#define T3_STAGE_VERSION 3u
#define T3_STAGE_COLUMNS 1024u
#define T3_STAGE_ROWS 240u
#define T3_STAGE_FLOOR_W 512u
#define T3_STAGE_FLOOR_H 256u
#define T3_STAGE_BAKE_HEIGHT 1100
/* Q16: the bake's elevation tangent of row 0, and rows per unit of tangent. */
#define T3_STAGE_TAN_TOP 52429
#define T3_STAGE_ROWS_PER_TAN 13107200
#define T3_STAGE_HEAD_BYTES 2048u
#define T3_STAGE_FLOOR_PALETTE_AT 512u
#define T3_STAGE_PICTURE_PALETTE_AT 1024u
#define T3_STAGE_FLOOR_AT T3_STAGE_HEAD_BYTES
#define T3_STAGE_FLOOR_BYTES (T3_STAGE_FLOOR_W*T3_STAGE_FLOOR_H)
#define T3_STAGE_PICTURE_AT (T3_STAGE_FLOOR_AT+T3_STAGE_FLOOR_BYTES)
#define T3_STAGE_PICTURE_BYTES (T3_STAGE_COLUMNS*T3_STAGE_ROWS)
#define T3_STAGE_FILE_BYTES (T3_STAGE_PICTURE_AT+T3_STAGE_PICTURE_BYTES)
enum {
    T3_STAGE_WORD_MAGIC, T3_STAGE_WORD_VERSION, T3_STAGE_WORD_NUMBER,
    T3_STAGE_WORD_BOX_HALF,      /* panorama box half-width, PS1 units */
    T3_STAGE_WORD_BACK,          /* clear colour, RGB555 */
    T3_STAGE_WORD_SHADOW,        /* drop shadow, RGB555 */
    T3_STAGE_WORD_OGRE_LEVEL,    /* floor brightness in a True Ogre fight, 128 unchanged */
    T3_STAGE_WORDS=16
};
#endif
