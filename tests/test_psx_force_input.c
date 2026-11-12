/* Regression: Force has a camera view but no duel camera director. Human
 * physical input must still follow the current target's projected side. */
#include "psx_match.h"
#include <assert.h>
#include <string.h>

enum { BRANCH=64, MOVE=76, INDEX=132, MOTION=INDEX+4023*4, BYTES=MOTION+52 };
static uint8_t bank_bytes[BYTES],trig[0x2800],atan_bytes[1025],push[20];
static t3_psx_match_t match;
static void put16(uint8_t* p,unsigned x){p[0]=(uint8_t)x;p[1]=(uint8_t)(x>>8);}
static void put32(uint8_t* p,unsigned x){put16(p,x);put16(p+2,x>>16);}

int main(void) {
    t3_divmot_t bank;
    bank_bytes[1]=0;put16(bank_bytes+2,1);
    put32(bank_bytes+4,BRANCH);put32(bank_bytes+8,MOVE);put32(bank_bytes+12,INDEX);
    for(unsigned i=3;i<7;++i)put32(bank_bytes+4+4*i,MOTION);
    for(unsigned i=7;i<15;++i)put32(bank_bytes+4+4*i,BYTES);
    for(unsigned i=0;i<4023;++i)put32(bank_bytes+INDEX+4*i,T3_DIVMOT_EMPTY_SLOT);
    put32(bank_bytes+INDEX+3*4,0);
    put32(bank_bytes+MOVE,2);put32(bank_bytes+MOVE+4,0x842);bank_bytes[MOVE+24]=4;
    put16(bank_bytes+BRANCH,0xC000);put16(bank_bytes+BRANCH+6,3);bank_bytes[BRANCH+8]=6;
    bank_bytes[MOTION+2]=4;
    put16(trig+0x800,4096);put16(trig+0x1800,(unsigned)-4096);
    assert(t3_divmot_open(&bank,bank_bytes,sizeof bank_bytes));
    t3_psx_match_config_t config={0};
    config.bank=&bank;config.scale_percent=100;config.max_health_q16=140*65536;
    config.ground_tables=(t3_psx_ground_tables_t){{trig,sizeof trig},atan_bytes,sizeof atan_bytes,push,sizeof push,NULL};
    config.enable_rounds=2;config.start_force=1;
    for(unsigned i=0;i<3;++i)config.spawn[i]=(t3_psx_spawn_t){
        {(int)i*1000,0,0},{(int)i*1000,0,0},0x4000,0x4000};
    const t3_input_t neutral={0,0};
    /* InputUpdate reads side after this tick's animation. Test both sides
     * with the same physical right+left-punch and verify command history. */
    for(unsigned side=0;side<2;++side) {
        assert(t3_psx_match_init(&match,&config));
        match.screen_known=1;match.screen_x[0]=200;
        match.screen_x[1]=match.screen_x[2]=(int16_t)(side?100:300);
        t3_psx_match_frame_t frame={.frame_counter=1,.physical_inputs=1,
            .physical={0x2080,0,0},.pressed={0x80,0,0}};
        assert(t3_psx_match_step_original(&match,neutral,neutral,&frame));
        const t3_command_frame_t input=t3_history_at(&match.fighter[0].input,0);
        assert(input.direction==(side?4:6));
        assert(input.held_buttons==1 && input.pressed_buttons==1);
        assert(match.fighter[0].side_flag==side);
    }
    /* A pickup must record the projected head position, rather than the
     * zero screen coordinates previously passed into the level runner. */
    static uint8_t tables[T3_FORCE_TABLE_BYTES],script[T3_FORCE_SCRIPT_BYTES];
    put32(script,0x7fffffff);
    config.force_tables=tables;config.force_script=script;
    assert(t3_psx_match_init(&match,&config));
    match.force_view=(t3_psx_camera_matrix_t){.m={{4096,0,0},{0,4096,0},{0,0,4096}},
        .t={0,0,10000},.h=500};
    match.force.item[0]=(t3_force_item_t){.state=1};
    const t3_psx_match_frame_t pickup={.frame_counter=1};
    assert(t3_psx_match_step_original(&match,neutral,neutral,&pickup));
    assert(match.force.item[0].state==2);
    /* GTE reciprocal projection rounds the negative vertical product down. */
    assert(match.force.player.screen_x==192 && match.force.player.screen_z==224);
    assert(match.force.item[0].popup_x==156 && match.force.item[0].popup_y==3584);
    assert(match.fighter[0].health_q16==190*65536);
    match.force.slot[0].state=5;
    match.fighter[1].health_q16=0;
    match.fighter[1].last_attacker=0;
    const t3_psx_match_frame_t defeated={.frame_counter=2};
    assert(t3_psx_match_step_original(&match,neutral,neutral,&defeated));
    assert(match.force.slot[0].state==6);
    assert(match.force.fighter[0].screen_x==192 && match.force.fighter[0].screen_y==224);
    assert(match.force.slot[0].popup_x==165 && match.force.slot[0].popup_y==3584);
    /* Without either camera path, the physical-input API remains invalid. */
    config.start_force=0;
    assert(t3_psx_match_init(&match,&config));
    const t3_psx_match_frame_t bad={.physical_inputs=1};
    assert(!t3_psx_match_step_original(&match,neutral,neutral,&bad));
    return 0;
}
