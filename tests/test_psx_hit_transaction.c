#include "psx_match.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
enum {B0=64,B1=76,B2=B1+112,B6=B2+4023*4,BYTES=B6+52};
static uint8_t bytes[BYTES],trig[0x2800],atan_bytes[1025],reactions[44];
static void p16(uint8_t* p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void p32(uint8_t* p,uint32_t n){p16(p,(uint16_t)n);p16(p+2,(uint16_t)(n>>16));}
static void setup(t3_divmot_t* bank,t3_psx_match_config_t* c,t3_psx_match_t* game) {
    memset(bytes,0,sizeof(bytes));bytes[1]=0;p16(bytes+2,2);
    p32(bytes+4,B0);p32(bytes+8,B1);p32(bytes+12,B2);
    for(unsigned i=3;i<7;++i)p32(bytes+4+4*i,B6);
    for(unsigned i=7;i<15;++i)p32(bytes+4+4*i,BYTES);
    for(unsigned i=0;i<4023;++i)p32(bytes+B2+4*i,T3_DIVMOT_EMPTY_SLOT);
    p32(bytes+B2+3*4,0);p32(bytes+B2+21*4,1);p32(bytes+B2+0xD66*4,1);
    for(unsigned i=0;i<2;++i) {
        uint8_t* row=bytes+B1+56*i;p32(row,2);p32(row+4,0x842);row[24]=4;
        p16(row+8,0x412);p16(row+20,10);p16(row+50,7);
        p16(row+16,i?3:0);
    }
    bytes[B6+2]=4;p16(trig+0x800,4096);p16(trig+0x1800,(uint16_t)-4096);
    p16(reactions,7);for(unsigned i=0;i<14;++i)p16(reactions+2+2*i,21);
    assert(t3_divmot_open(bank,bytes,sizeof(bytes)));
    *c=(t3_psx_match_config_t){0};c->bank=bank;c->max_health_q16=140*65536;c->scale_percent=100;
    c->ground_tables=(t3_psx_ground_tables_t){{trig,sizeof(trig)},atan_bytes,sizeof(atan_bytes),NULL,0,NULL};
    c->spawn[0]=(t3_psx_spawn_t){{-100,0,0},{-100,0,0},0x4000,0x4000};
    c->spawn[1]=(t3_psx_spawn_t){{100,0,0},{100,0,0},0xC000,0xC000};
    assert(t3_psx_match_init(game,c));
    c->enable_combat=1;c->reactions=reactions;c->reactions_size=sizeof(reactions);c->collapse_on_ko=1;
    for(unsigned i=0;i<2;++i) {
        game->fighter[i].ground.damage=10;game->fighter[i].vm.clock.pose_frame=4;
        game->fighter[i].ground.guard=0;
    }
    game->fighter[0].hit_gate=(t3_psx_hit_test_gate_t){0};
    game->fighter[0].hit_gate.status=T3_HIT_GATE_ACCEPT;
    game->fighter[0].hit_gate.hit_slot=1;game->fighter[0].hit_gate.zone=8;
    game->fighter[0].hit_gate.hit_freeze_in=3;game->fighter[0].hit_gate.hit_cooldown=4;
}
int main(void) {
    t3_divmot_t bank;t3_psx_match_config_t c;t3_psx_match_t game,before;
    setup(&bank,&c,&game);
    assert(t3_psx_match_apply_hits(&game));
    assert(game.fighter[1].health_q16==127*65536 && game.fighter[0].health_q16==140*65536);
    assert(game.fighter[0].hit_done && game.fighter[1].got_hit && game.fighter[1].hit_freeze_in==3);
    assert(game.fighter[1].vm.pending_slot==21 && game.fighter[1].vm.queued.transition==42);
    assert(game.fighter[1].vm.queued.branch_kind==1 && game.fighter[1].ground.last_damage==13);
    game.fighter[0].hit_gate.status=T3_HIT_GATE_NO_ATTACK;
    game.fighter[1].last_counter=1;
    assert(t3_psx_match_apply_hits(&game));
    assert(!game.fighter[1].last_counter && !game.fighter[1].last_guarded);
    setup(&bank,&c,&game);game.fighter[1].ground.guard=0x10;
    assert(t3_psx_match_apply_hits(&game));
    assert(game.fighter[1].health_q16==140*65536 && game.fighter[1].hit.guarded);
    assert(game.fighter[1].vm.queued.transition==35 && game.fighter[1].registered_blocks==1);
    setup(&bank,&c,&game);game.fighter[1].hit_gate=game.fighter[0].hit_gate;
    assert(t3_psx_match_apply_hits(&game));
    assert(game.fighter[0].health_q16==127*65536 && game.fighter[1].health_q16==127*65536);
    setup(&bank,&c,&game);c.reactions_size=0;before=game;
    assert(!t3_psx_match_apply_hits(&game) && !memcmp(&game,&before,sizeof(game)));
    setup(&bank,&c,&game);game.fighter[1].health_q16=5*65536;
    game.fighter[1].taps.condition_used=1;
    game.fighter[1].taps.both=1;
    assert(t3_psx_match_apply_hits(&game));
    assert(game.fighter[1].health_q16==0 && game.fighter[1].hit.ko_started);
    assert(game.fighter[1].vm.slot==0xD66 && game.fighter[1].vm.clock.pose_frame==1);
    assert(game.fighter[1].vm.current_transition==6 && !game.fighter[1].vm.pending_valid);
    assert(game.fighter[1].ko_collapse);
    assert(!game.fighter[1].taps.condition_used && game.fighter[1].taps.both==1);
    /* FighterOpponent may select the defender itself. Clearing its incoming
     * slots must then preserve the actual attacker's contact/hit-done flags. */
    setup(&bank,&c,&game);
    p16(bytes+B0,16);bytes[B0+3]=0x45;p16(bytes+B0+6,21);
    bytes[B0+8]=33;bytes[B0+9]=0;bytes[B0+10]=255;bytes[B0+11]=1;
    game.fighter[1].hit_gate=game.fighter[0].hit_gate;
    game.fighter[0].hit_gate.status=T3_HIT_GATE_NO_ATTACK;
    game.fighter[0].ground.relative_angle=game.fighter[1].ground.relative_angle=0;
    t3_history_clear(&game.fighter[0].input);
    assert(t3_psx_match_reversals(&game));
    assert(game.fighter[0].vm.pending_slot==21 && game.fighter[0].ground.damage_override==30);
    assert(game.fighter[1].contact && game.fighter[1].hit_done);
    assert(t3_psx_match_apply_hits(&game));
    assert(game.fighter[0].health_q16==140*65536 && !game.fighter[0].got_hit);
    assert(game.fighter[1].contact && game.fighter[1].hit_done && !game.reversal_cancelled);
    return 0;
}
