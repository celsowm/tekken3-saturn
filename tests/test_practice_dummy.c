#include "practice_dummy.h"
#include <assert.h>
int main(void){
    t3_practice_dummy_facts_t f={.attack=0x10,.pose_frame=5,.active_last=10};
    assert(t3_practice_dummy_input(&f,T3_DUMMY_STAND).word==0);
    assert(t3_practice_dummy_input(&f,T3_DUMMY_CROUCH).word==T3_DUMMY_DOWN);
    assert(t3_practice_dummy_input(&f,T3_DUMMY_STAND_GUARD).word==T3_DUMMY_BACK);
    f.pose_frame=11;                                   /* the active window ended */
    assert(t3_practice_dummy_input(&f,T3_DUMMY_STAND_GUARD).word==0);
    f.pose_frame=5;f.attack=0x08;
    assert(t3_practice_dummy_input(&f,T3_DUMMY_STAND_GUARD).word==0);
    assert(t3_practice_dummy_input(&f,T3_DUMMY_CROUCH_GUARD).word==(T3_DUMMY_DOWN|T3_DUMMY_BACK));
    assert(t3_practice_dummy_input(&f,T3_DUMMY_GUARD_ALL).word==(T3_DUMMY_DOWN|T3_DUMMY_BACK));
    f.attack=0x10;assert(t3_practice_dummy_input(&f,T3_DUMMY_GUARD_ALL).word==T3_DUMMY_BACK);
    assert(t3_practice_dummy_input(&f,T3_DUMMY_AUTO_GUARD).human_guard==1);
    f.lying=1;                                          /* on the ground the dummy holds up */
    assert(t3_practice_dummy_input(&f,T3_DUMMY_CROUCH).word==T3_DUMMY_UP);
    f.lying=0;f.in_reaction=1;f.has_air_window=1;f.has_tech_roll=1;f.frame_odd=1;
    assert(t3_practice_dummy_input(&f,T3_DUMMY_UKEMI_MAE).word==0x60);
    assert(t3_practice_dummy_input(&f,T3_DUMMY_UKEMI_OKU).word==0x90);
    f.frame_odd=0;assert(t3_practice_dummy_input(&f,T3_DUMMY_UKEMI_MAE).word==0);
    assert(t3_practice_dummy_input(&f,T3_DUMMY_CONTROLLER).human_guard==1);
    return 0;
}
