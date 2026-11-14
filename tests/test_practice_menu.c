#include "practice_menu.h"
#include <assert.h>
static void go_to(t3_practice_menu_t* m,unsigned row){
    uint8_t rows[T3_PM_MAX_ROWS];
    for(unsigned guard=0;guard<16;++guard){
        t3_practice_menu_rows(m,rows);
        if(rows[m->cursor]==row)return;
        t3_practice_menu_step(m,T3_PM_DOWN);
    }
    assert(!"row not found");
}
int main(void){
    t3_practice_menu_t m;t3_practice_menu_reset(&m);
    assert(!m.open && m.attack_data==1 && m.key_display==1 && m.dummy==T3_DUMMY_STAND && !m.counter_attacks);
    assert(m.cpu_difficulty==1 && m.cpu_level==2);
    assert(t3_practice_menu_step(&m,T3_PM_DOWN)==T3_PM_NONE && m.cursor==0);   /* closed: no input */
    t3_practice_menu_step(&m,T3_PM_START);assert(m.open);
    t3_practice_menu_step(&m,T3_PM_UP);                                          /* wraps to the last row */
    uint8_t rows[T3_PM_MAX_ROWS];unsigned n=t3_practice_menu_rows(&m,rows);
    assert(m.cursor==n-1u && rows[m.cursor]==T3_PM_ROW_EXIT);
    t3_practice_menu_step(&m,T3_PM_DOWN);assert(m.cursor==0);
    go_to(&m,T3_PM_ROW_DUMMY);
    t3_practice_menu_step(&m,T3_PM_LEFT);assert(m.dummy==T3_DUMMY_CONTROLLER);   /* wraps below 0 */
    t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.dummy==T3_DUMMY_STAND);
    go_to(&m,T3_PM_ROW_COUNTER);t3_practice_menu_step(&m,T3_PM_OK);assert(m.counter_attacks==1);
    go_to(&m,T3_PM_ROW_ATTACK_DATA);t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.attack_data==0);
    go_to(&m,T3_PM_ROW_FREEZE_SIGNAL);t3_practice_menu_step(&m,T3_PM_OK);assert(m.freeze_signal==1);
    go_to(&m,T3_PM_ROW_REPLAY);
    for(int i=0;i<4;++i)t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.replay_setting==4);                      /* MANUAL */
    t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.replay_setting==0);                                          /* wraps */
    go_to(&m,T3_PM_ROW_KEY_DISPLAY);t3_practice_menu_step(&m,T3_PM_OK);assert(m.key_display==0);
    /* VS CPU: the dummy page gives way to the CPU's, counters are cleared. */
    go_to(&m,T3_PM_ROW_MODE);t3_practice_menu_step(&m,T3_PM_RIGHT);
    assert(m.sub==T3_PM_SUB_VS_CPU && m.counter_attacks==0);
    n=t3_practice_menu_rows(&m,rows);
    assert(n==9 && rows[1]==T3_PM_ROW_CPU_DIFFICULTY && rows[2]==T3_PM_ROW_CPU_LEVEL);
    go_to(&m,T3_PM_ROW_CPU_DIFFICULTY);t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.cpu_difficulty==2);
    t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.cpu_difficulty==0);
    go_to(&m,T3_PM_ROW_CPU_LEVEL);
    for(int i=0;i<8;++i)t3_practice_menu_step(&m,T3_PM_RIGHT);
    assert(m.cpu_level==0);                                                      /* 2 + 8 wraps at 10 */
    /* COMBO TRAINING: the player and the combo of that character. */
    m.combo_count[0]=3;m.combo_count[1]=0;
    go_to(&m,T3_PM_ROW_MODE);t3_practice_menu_step(&m,T3_PM_RIGHT);
    assert(m.sub==T3_PM_SUB_COMBO && m.key_display==1 && m.combo_type==0);
    n=t3_practice_menu_rows(&m,rows);
    assert(n==8 && rows[1]==T3_PM_ROW_COMBO_PLAYER && rows[2]==T3_PM_ROW_COMBO_TYPE);
    go_to(&m,T3_PM_ROW_COMBO_TYPE);
    t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.combo_type==1);
    t3_practice_menu_step(&m,T3_PM_RIGHT);t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.combo_type==0);   /* wraps at 3 */
    t3_practice_menu_step(&m,T3_PM_LEFT);assert(m.combo_type==2);
    go_to(&m,T3_PM_ROW_COMBO_PLAYER);t3_practice_menu_step(&m,T3_PM_RIGHT);
    assert(m.combo_player==1 && m.combo_type==0);
    go_to(&m,T3_PM_ROW_COMBO_TYPE);t3_practice_menu_step(&m,T3_PM_RIGHT);assert(m.combo_type==0);          /* none to pick */
    go_to(&m,T3_PM_ROW_MODE);t3_practice_menu_step(&m,T3_PM_RIGHT);
    assert(m.sub==T3_PM_SUB_FREE && m.attack_data==1);                           /* FREE turns ATTACK DATA on */
    go_to(&m,T3_PM_ROW_SELECT);assert(t3_practice_menu_step(&m,T3_PM_OK)==T3_PM_CHARACTER_SELECT && !m.open);
    t3_practice_menu_step(&m,T3_PM_START);go_to(&m,T3_PM_ROW_EXIT);
    assert(t3_practice_menu_step(&m,T3_PM_OK)==T3_PM_LEAVE);
    return 0;
}
