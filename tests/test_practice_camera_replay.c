#include "psx_camera.h"
#include <assert.h>
#include <string.h>
/* A practice replay ends inside the running fight: the replay camera's weight, `enabled` and the replay flag must all go back to
 * the fight camera's (the KO replay is followed by a round, so nothing else restores them). */
int main(void){
    t3_psx_camera_t c;t3_psx_camera_fighter_t f[2];
    memset(&c,0,sizeof c);memset(f,0,sizeof f);
    f[0].root_x=-1000;f[1].root_x=1000;f[0].pos_x=-1000;f[1].pos_x=1000;
    /* the state t3_psx_camera_replay_start and _step leave */
    c.replay_on=1;c.enabled=0;c.weight[0]=0;c.weight[1]=4096;c.state=6;
    t3_psx_camera_replay_end(&c,f);
    assert(!c.replay_on && c.enabled==1);
    assert(c.weight[0]==4096 && c.weight[1]==0 && c.weight[2]==0);
    assert(c.state==1);
    /* with no replay running it changes nothing */
    c.weight[0]=1234;
    t3_psx_camera_replay_end(&c,f);
    assert(c.weight[0]==1234);
    return 0;
}
