#include "practice_menu.h"
#include <assert.h>
/* Opens the menu on `sub`'s page. */
static void open_page(t3_practice_menu_t* m,unsigned sub) {
    if(!m->open)t3_practice_menu_step(m,T3_PM_START);
    if(!m->mode_select){
        uint8_t rows[T3_PM_MAX_ROWS];
        const unsigned n=t3_practice_menu_rows(m,rows);
        while(rows[m->cursors[m->sub]]!=T3_PM_ROW_RETURN)t3_practice_menu_step(m,T3_PM_DOWN);
        (void)n;
        t3_practice_menu_step(m,T3_PM_OK);
    }
    while(m->select_cursor!=sub)t3_practice_menu_step(m,T3_PM_DOWN);
    t3_practice_menu_step(m,T3_PM_OK);
    assert(!m->mode_select && m->sub==sub);
}
static void go_to(t3_practice_menu_t* m,unsigned row){
    uint8_t rows[T3_PM_MAX_ROWS];
    for(unsigned guard=0;guard<16;++guard){
        t3_practice_menu_rows(m,rows);
        if(rows[t3_practice_menu_cursor(m)]==row)return;
        t3_practice_menu_step(m,T3_PM_DOWN);
    }
    assert(!"row not found");
}
int main(void){
    t3_practice_menu_t m;t3_practice_menu_reset(&m);
    assert(!m.open && m.attack_data==1 && m.key_display==1 && m.dummy==T3_DUMMY_STAND && !m.counter_attacks);
    assert(m.cpu_difficulty==1 && m.cpu_level==2 && m.mode_select && m.changed);
    assert(t3_practice_menu_step(&m,T3_PM_DOWN)==T3_PM_NONE && m.select_cursor==0);   /* closed: no input */
    t3_practice_menu_step(&m,T3_PM_START);assert(m.open && m.mode_select);
    /* MODE SELECT: five rows, wrapping. */
    t3_practice_menu_step(&m,T3_PM_UP);assert(m.select_cursor==T3_PM_SELECT_RESET);
    t3_practice_menu_step(&m,T3_PM_DOWN);assert(m.select_cursor==0);
    /* A face button chooses FREE: its page opens on OK/CANCEL, the CANCEL reading gone. */
    t3_practice_menu_step(&m,T3_PM_OK);assert(!m.mode_select && m.sub==T3_PM_SUB_FREE && !m.changed);
    uint8_t rows[T3_PM_MAX_ROWS];unsigned n=t3_practice_menu_rows(&m,rows);
    assert(n==9 && rows[0]==T3_PM_ROW_OK_CANCEL && rows[1]==T3_PM_ROW_COMMAND_LIST && rows[2]==T3_PM_ROW_DUMMY &&
           rows[3]==T3_PM_ROW_COUNTER && rows[8]==T3_PM_ROW_RETURN);
    t3_practice_menu_step(&m,T3_PM_UP);assert(t3_practice_menu_cursor(&m)==n-1u);                 /* wraps to the last row */
    t3_practice_menu_step(&m,T3_PM_DOWN);assert(t3_practice_menu_cursor(&m)==0);
    go_to(&m,T3_PM_ROW_DUMMY);
    t3_practice_menu_step(&m,T3_PM_LEFT);assert(m.dummy==T3_DUMMY_CONTROLLER && !m.changed);   /* wraps below 0 */
    t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.dummy==T3_DUMMY_STAND);
    t3_practice_menu_step(&m,T3_PM_OK);assert(t3_practice_menu_cursor(&m)==0);                 /* a face button: back to the first row */
    go_to(&m,T3_PM_ROW_COUNTER);t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.counter_attacks==1);
    go_to(&m,T3_PM_ROW_ATTACK_DATA);t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.attack_data==0);
    go_to(&m,T3_PM_ROW_FREEZE_SIGNAL);t3_practice_menu_step(&m,T3_PM_LEFT);assert(m.freeze_signal==1);
    go_to(&m,T3_PM_ROW_REPLAY);
    for(int i=0;i<4;++i)t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.replay_setting==4);                      /* MANUAL */
    t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.replay_setting==0);                                          /* wraps */
    /* COMMAND LIST: opens with a button when the character has lines; the cursor wraps and pages by five. */
    m.command_count[0]=12;
    go_to(&m,T3_PM_ROW_COMMAND_LIST);t3_practice_menu_step(&m,T3_PM_OK);assert(m.command_open && m.command_cursor==0);
    t3_practice_menu_step(&m,T3_PM_UP);assert(m.command_cursor==11);
    assert(m.command_offset==-42);                                                           /* -64 eased by a third of it, plus a pixel */
    for(int i=0;i<12;++i)t3_practice_menu_step(&m,0);assert(m.command_offset==0);              /* and out in a dozen frames */
    t3_practice_menu_step(&m,T3_PM_DOWN);assert(m.command_cursor==0 && m.command_offset==42);
    for(int i=0;i<12;++i)t3_practice_menu_step(&m,0);
    m.command_two=1;m.command_count[1]=7;m.pressed2=T3_PM_DOWN;                               /* the dummy's list (CONTROLLER) moves on pad 2 */
    t3_practice_menu_step(&m,0);m.pressed2=0;assert(m.command_cursor2==1 && m.command_cursor==0);
    m.command_two=0;
    t3_practice_menu_step(&m,T3_PM_RIGHT);t3_practice_menu_step(&m,T3_PM_RIGHT);t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.command_cursor==15%12);
    t3_practice_menu_step(&m,T3_PM_START);assert(m.open && !m.command_open);                  /* START closes the list, not the menu */
    m.command_count[0]=0;t3_practice_menu_step(&m,T3_PM_OK);assert(!m.command_open);          /* nothing to show */
    go_to(&m,T3_PM_ROW_KEY_DISPLAY);t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.key_display==0);
    /* OK/CANCEL resumes the fight; the menu comes back on the page it left, with CANCEL again. */
    go_to(&m,T3_PM_ROW_OK_CANCEL);t3_practice_menu_step(&m,T3_PM_OK);assert(!m.open);
    t3_practice_menu_step(&m,T3_PM_START);assert(m.open && !m.mode_select && m.changed && m.sub==T3_PM_SUB_FREE);
    /* RETURN TO MODE SELECT: the cursor stands on the sub-mode; moving it onto VS CPU resets that sub-mode. */
    go_to(&m,T3_PM_ROW_RETURN);t3_practice_menu_step(&m,T3_PM_OK);assert(m.mode_select && m.select_cursor==T3_PM_SUB_FREE);
    t3_practice_menu_step(&m,T3_PM_DOWN);assert(m.sub==T3_PM_SUB_VS_CPU && m.counter_attacks==0 && m.replay_setting==0);
    t3_practice_menu_step(&m,T3_PM_OK);
    n=t3_practice_menu_rows(&m,rows);
    assert(n==9 && rows[2]==T3_PM_ROW_CPU_DIFFICULTY && rows[3]==T3_PM_ROW_CPU_LEVEL && rows[7]==T3_PM_ROW_KEY_DISPLAY);
    go_to(&m,T3_PM_ROW_CPU_DIFFICULTY);t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.cpu_difficulty==2);
    t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.cpu_difficulty==0);
    go_to(&m,T3_PM_ROW_CPU_LEVEL);
    for(int i=0;i<8;++i)t3_practice_menu_step(&m,T3_PM_RIGHT);
    assert(m.cpu_level==0);                                                      /* 2 + 8 wraps at 10 */
    /* COMBO TRAINING: the player and the combo of that character. */
    m.combo_count[0]=3;m.combo_count[1]=0;
    open_page(&m,T3_PM_SUB_COMBO);
    assert(m.key_display==1 && m.combo_type==0);
    n=t3_practice_menu_rows(&m,rows);
    assert(n==8 && rows[2]==T3_PM_ROW_COMBO_PLAYER && rows[3]==T3_PM_ROW_COMBO_TYPE && rows[7]==T3_PM_ROW_RETURN);
    go_to(&m,T3_PM_ROW_COMBO_TYPE);
    t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.combo_type==1);
    t3_practice_menu_step(&m,T3_PM_RIGHT);t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.combo_type==0);   /* wraps at 3 */
    t3_practice_menu_step(&m,T3_PM_LEFT);assert(m.combo_type==2);
    go_to(&m,T3_PM_ROW_COMBO_PLAYER);t3_practice_menu_step(&m,T3_PM_RIGHT);
    assert(m.combo_player==1 && m.combo_type==0);
    go_to(&m,T3_PM_ROW_COMBO_TYPE);t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.combo_type==0);          /* none to pick */
    open_page(&m,T3_PM_SUB_FREE);
    assert(m.sub==T3_PM_SUB_FREE && m.attack_data==1);                           /* FREE turns ATTACK DATA on */
    /* PLAYER SELECT and RESET leave the fight. */
    go_to(&m,T3_PM_ROW_RETURN);t3_practice_menu_step(&m,T3_PM_OK);
    t3_practice_menu_step(&m,T3_PM_UP);t3_practice_menu_step(&m,T3_PM_UP);assert(m.select_cursor==T3_PM_SELECT_PLAYER);
    assert(t3_practice_menu_step(&m,T3_PM_OK)==T3_PM_CHARACTER_SELECT && !m.open);
    t3_practice_menu_step(&m,T3_PM_START);assert(m.open && m.mode_select);
    t3_practice_menu_step(&m,T3_PM_DOWN);assert(m.select_cursor==T3_PM_SELECT_RESET);
    assert(t3_practice_menu_step(&m,T3_PM_OK)==T3_PM_LEAVE);
    return 0;
}
