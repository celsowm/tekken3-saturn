#include "psx_head.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
static uint8_t trig[0x2800],atan[1025],sqrt_bytes[384];
int main(void) {
    t3_psx_body_tables_t tables={{{trig,sizeof(trig)},atan,sizeof(atan),NULL,0,NULL},sqrt_bytes,sizeof(sqrt_bytes)};
    t3_psx_joint_t own[T3_PSX_DRAW_PARTS]={0},opponent[T3_PSX_DRAW_PARTS]={0};
    t3_psx_head_t s={0},before;
    t3_psx_matrix_t m={{{4096,0,0},{0,4096,0},{0,0,4096}}},original=m;
    t3_move_record_t move={0};
    s.active=1;s.snap=1;
    /* A move without LOOK_AT clears active, preserving the pending snap. */
    assert(t3_psx_head_look(&s,&tables,own,opponent,&move,1,0,&m));
    assert(!s.active && s.snap && !memcmp(&m,&original,sizeof(m)));
    move.flags=0x40000000u;move.length=50;move.active_first=3;move.active_last=10;
    assert(t3_psx_head_look(&s,&tables,own,opponent,&move,32,0,&m));
    assert(!s.active && s.snap); /* max(active_last, length-17) = 33 */
    assert(t3_psx_head_look(&s,&tables,own,opponent,&move,33,0,&m));
    assert(s.active && !s.snap && !memcmp(s.current,s.target,sizeof(s.current)));
    /* Near-target hold delays retargeting; angular speed remains bounded. */
    s.current[1]=30000;s.current[2]=-30000;s.target[1]=s.target[2]=0;s.hold=0;
    assert(t3_psx_head_look(&s,&tables,own,opponent,&move,34,0,&m));
    assert(s.hold==1 && s.target[1]==0 && s.target[2]==0);
    assert(s.current[1]==30000-0x71C && s.current[2]==-30000+0x71C);
    s.hold=59;
    assert(t3_psx_head_look(&s,&tables,own,opponent,&move,35,0,&m));
    assert(!s.hold && s.target[1]!=0);
    before=s;original=m;
    assert(t3_psx_head_look(&s,&tables,own,opponent,&move,36,1,&m));
    assert(!memcmp(&s,&before,sizeof(s)) && !memcmp(&m,&original,sizeof(m)));
    tables.sqrt_size=0;
    assert(!t3_psx_head_look(&s,&tables,own,opponent,&move,36,0,&m));
    assert(!memcmp(&s,&before,sizeof(s)) && !memcmp(&m,&original,sizeof(m)));
    return 0;
}
