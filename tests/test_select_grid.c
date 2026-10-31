#include "select_grid.h"
#include <stdio.h>

static int g_failed;
#define CHECK(c) do{if(!(c)){printf("failed line %d: %s\n",__LINE__,#c);g_failed=1;}}while(0)

int main(void) {
    /* Every fighter has a cell and can be found again. */
    for(unsigned id=0;id<T3_SELECT_FIGHTERS;++id)
        CHECK(t3_select_id(t3_select_find(id))==id);
    /* Rows wrap through the hidden Gon cell, from either end. */
    t3_select_cursor_t c=t3_select_find(9);
    CHECK(c.row==0 && c.column==T3_SELECT_DRAWN_COLUMNS-1);
    c=t3_select_move(c,1,0);
    CHECK(c.column==T3_SELECT_DRAWN_COLUMNS && t3_select_id(c)==17);
    c=t3_select_move(c,1,0);
    CHECK(c.column==0 && t3_select_id(c)==7);
    c=t3_select_move(c,-1,0);
    CHECK(t3_select_id(c)==17);
    /* Down and up keep the column; Gon sits at the end of both rows. */
    c=t3_select_move(c,0,1);
    CHECK(c.row==1 && t3_select_id(c)==17);
    c=t3_select_move((t3_select_cursor_t){0,6},0,1);
    CHECK(t3_select_id(c)==10);
    c=t3_select_move(c,0,-1);
    CHECK(t3_select_id(c)==0);
    /* True Ogre borrows Ogre's portrait; ids 0-19 have their own. */
    CHECK(t3_select_face(20)==14);
    for(unsigned id=0;id<20u;++id)CHECK(t3_select_face(id)==id);
    if(!g_failed)printf("select grid ok\n");
    return g_failed;
}
