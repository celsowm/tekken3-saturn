#ifndef T3_GAME_SELECT_GRID_H
#define T3_GAME_SELECT_GRID_H
/* The character select cells and cursor. Rules from Tekken3Remake
 * docs/research/code/modes.md (Character select). PolyForm Noncommercial 1.0.0,
 * NOTICE.md. Required Notice: Copyright (c) 2026 n0madic. */
#include <stdint.h>

enum {
    T3_SELECT_ROWS=2,
    T3_SELECT_COLUMNS=11,      /* each row's ring, ending in the hidden Gon */
    T3_SELECT_DRAWN_COLUMNS=10,
    T3_SELECT_FIGHTERS=21
};

typedef struct { uint8_t row,column; } t3_select_cursor_t;

/* The character id of a cell. */
unsigned t3_select_id(t3_select_cursor_t cursor);
/* Up and down keep the column; left and right wrap through the hidden cell. */
t3_select_cursor_t t3_select_move(t3_select_cursor_t cursor,int across,int down);
/* The first cell of a character id; an unknown id gives the first cell. */
t3_select_cursor_t t3_select_find(unsigned id);
/* The HUD portrait of a character id: True Ogre has none and shows Ogre's. */
unsigned t3_select_face(unsigned id);

#endif
