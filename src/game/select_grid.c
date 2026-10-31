#include "select_grid.h"

enum { GON=17, OGRE=14, TRUE_OGRE=20 };

/* The list at Rev.1 0x800B9638. */
static const uint8_t g_cells[T3_SELECT_ROWS][T3_SELECT_COLUMNS]={
    {7,4,5,1,6,8,0,3,2,9,GON},
    {19,12,11,13,OGRE,TRUE_OGRE,10,16,15,18,GON},
};

unsigned t3_select_id(t3_select_cursor_t cursor) {return g_cells[cursor.row][cursor.column];}

t3_select_cursor_t t3_select_move(t3_select_cursor_t cursor,int across,int down) {
    if(down)cursor.row^=1u;
    if(across>0)cursor.column=(uint8_t)((cursor.column+1u)%T3_SELECT_COLUMNS);
    if(across<0)cursor.column=(uint8_t)((cursor.column+T3_SELECT_COLUMNS-1u)%T3_SELECT_COLUMNS);
    return cursor;
}

t3_select_cursor_t t3_select_find(unsigned id) {
    for(uint8_t row=0;row<T3_SELECT_ROWS;++row)
        for(uint8_t column=0;column<T3_SELECT_COLUMNS;++column)
            if(g_cells[row][column]==id)return (t3_select_cursor_t){row,column};
    return (t3_select_cursor_t){0,0};
}

unsigned t3_select_face(unsigned id) {return id==TRUE_OGRE?OGRE:id;}
