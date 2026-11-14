#include "practice_guide.h"
#include <assert.h>
#include <string.h>

static void p16(uint8_t* p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t* p,uint32_t v){p16(p,(uint16_t)v);p16(p+2,(uint16_t)(v>>16));}
int main(void) {
    /* T3CB: two bank types; the first has no combos, the second one combo of two steps and two move slots. */
    uint8_t blob[64];unsigned n=0;
    memcpy(blob,"T3CB",4);n=4;p16(blob+n,2);n+=2;
    p16(blob+n,0);n+=2;                                   /* kind 0: none */
    p16(blob+n,1);n+=2;                                   /* kind 1: one combo */
    p16(blob+n,2);p16(blob+n+2,2);n+=4;
    p32(blob+n,0x00030200u);n+=4;                         /* right held for 3 frames */
    p32(blob+n,0x00020008u);n+=4;                         /* LP for 2 frames */
    p16(blob+n,0x2C7);p16(blob+n+2,0x12E);n+=4;
    assert(t3_practice_combo_count(blob,n,0)==0 && t3_practice_combo_count(blob,n,1)==1 && t3_practice_combo_count(blob,n,5)==0);
    t3_practice_combo_t combo;
    assert(t3_practice_combo_get(blob,n,1,0,&combo) && combo.n_steps==2 && combo.n_moves==2);
    assert(!t3_practice_combo_get(blob,n,1,1,&combo) && !t3_practice_combo_get(blob,n,0,0,&combo));
    assert(t3_practice_combo_get(blob,n,1,0,&combo));
    assert(!t3_practice_combo_get(blob,n-1,1,0,&combo));                              /* truncated */

    /* COMBO TRAINING: IDLE loads the guide, READY waits for Select, STAND for the stance, PLAY scripts the steps. */
    t3_practice_guide_t g;memset(&g,0,sizeof g);t3_practice_guide_reset(&g);
    t3_practice_guide_in_t in={.sub=2,.combo_known=1,.combo=&combo,.cur_slot=3,.stance_slot=3};
    t3_practice_guide_step(&g,&in);
    assert(g.state==T3_GUIDE_READY && g.guide_length==2 && g.ring.head==1 && g.move_count==2 && g.marks[0]==0x2C7);
    t3_practice_guide_step(&g,&in);assert(g.state==T3_GUIDE_READY);
    in.select_pressed=1;t3_practice_guide_step(&g,&in);
    assert(g.state==T3_GUIDE_STAND && g.prompt==T3_GUIDE_PROMPT_GUIDE && g.scripted);
    in.select_pressed=0;in.turned=1;t3_practice_guide_step(&g,&in);                   /* turned away: turns back first */
    assert(g.state==T3_GUIDE_STAND && g.script==0x8000);
    in.turned=0;t3_practice_guide_step(&g,&in);assert(g.state==T3_GUIDE_PLAY);
    /* The original rewinds before it loads the ring, so the first run starts from a stale word and plays step 1 only. */
    for(int i=0;i<2;++i){t3_practice_guide_step(&g,&in);assert(g.script==0x0080 && g.state==T3_GUIDE_PLAY && g.scripted);}
    t3_practice_guide_step(&g,&in);                                                    /* the ring is played */
    assert(g.state==T3_GUIDE_IDLE && g.prompt==T3_GUIDE_PROMPT_END && g.prompt_timer==0x60);
    assert(!g.scripted);                                                               /* the pad is the player's again */
    /* The next run rewinds to the loaded ring and plays both steps. */
    t3_practice_guide_step(&g,&in);assert(g.state==T3_GUIDE_READY);
    in.select_pressed=1;t3_practice_guide_step(&g,&in);in.select_pressed=0;
    t3_practice_guide_step(&g,&in);assert(g.state==T3_GUIDE_PLAY);
    for(int i=0;i<3;++i){t3_practice_guide_step(&g,&in);assert(g.script==0x2000 && g.state==T3_GUIDE_PLAY);}
    for(int i=0;i<2;++i){t3_practice_guide_step(&g,&in);assert(g.script==0x0080 && g.state==T3_GUIDE_PLAY);}
    t3_practice_guide_step(&g,&in);assert(g.state==T3_GUIDE_IDLE && g.prompt==T3_GUIDE_PROMPT_END);
    /* A clean hit by the guided fighter ends the guide with the hit prompt. */
    t3_practice_guide_step(&g,&in);in.select_pressed=1;t3_practice_guide_step(&g,&in);in.select_pressed=0;
    t3_practice_guide_step(&g,&in);t3_practice_guide_step(&g,&in);in.hit_clean=1;t3_practice_guide_step(&g,&in);
    assert(g.state==T3_GUIDE_IDLE && g.prompt==T3_GUIDE_PROMPT_HIT);
    /* Lying down, the guided fighter holds up and the guide waits. */
    in.hit_clean=0;in.lying=1;t3_practice_guide_step(&g,&in);in.select_pressed=1;t3_practice_guide_step(&g,&in);
    in.select_pressed=0;t3_practice_guide_step(&g,&in);
    assert(g.state==T3_GUIDE_STAND && g.script==0x1000);
    t3_practice_guide_stop(&g);assert(g.state==T3_GUIDE_IDLE && !g.scripted);

    /* FREE: Select + down records, Select again ends the recording, Select replays it (mirrored for side 1). */
    memset(&g,0,sizeof g);t3_practice_guide_reset(&g);
    t3_practice_guide_in_t f={.sub=0,.guide_on=1,.cur_slot=3,.stance_slot=3,.me_cur_slot=3,.me_stance_slot=3};
    t3_practice_guide_step(&g,&f);assert(g.state==T3_GUIDE_READY);
    f.select_pressed=1;f.held_down=1;t3_practice_guide_step(&g,&f);assert(g.state==T3_GUIDE_RECORD_WAIT);
    f.select_pressed=0;t3_practice_guide_step(&g,&f);assert(g.state==T3_GUIDE_RECORD_WAIT);   /* waits for a key */
    f.mapped_record_key=1;f.pad_word=0x2000;
    t3_practice_guide_step(&g,&f);assert(g.state==T3_GUIDE_RECORDING);
    f.pad_word=0x2000|0x80;t3_practice_guide_step(&g,&f);
    f.select_pressed=1;t3_practice_guide_step(&g,&f);assert(g.state==T3_GUIDE_READY && g.ring.head>=1);
    f.select_pressed=1;f.held_down=0;t3_practice_guide_step(&g,&f);assert(g.state==T3_GUIDE_REPLAY);
    f.select_pressed=0;t3_practice_guide_step(&g,&f);assert(g.script==0x2000);
    for(int i=0;i<40 && g.state==T3_GUIDE_REPLAY;++i)t3_practice_guide_step(&g,&f);
    assert(g.state==T3_GUIDE_REPLAY_END);
    t3_practice_guide_step(&g,&f);assert(g.state==T3_GUIDE_IDLE && g.guide_prompt==1);
    return 0;
}
