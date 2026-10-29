#include "psx_replay_sounds.h"
#include <stdio.h>
#include <inttypes.h>
int main(void) {
    uint32_t ring[T3_REPLAY_SOUND_FRAMES]={0};int32_t delay=0;
    unsigned op,a,b,c,d,e;
    while(scanf("%u %u %u %u %u %u",&op,&a,&b,&c,&d,&e)==6) {
        int ok=1;t3_psx_replay_sound_t event={0};
        switch(op) {
        case 0:delay=(int32_t)a;
            for(unsigned i=0;i<T3_REPLAY_SOUND_FRAMES;++i)ring[i]=(uint32_t)b;
            break;
        case 1:ok=t3_psx_replay_sound_record(ring,&delay,(int32_t)a,b,c,d,e);break;
        case 2:ok=t3_psx_replay_sound_clear(ring,&delay,a);break;
        case 3:ok=t3_psx_replay_sound_at(ring,a,&event);break;
        default:return 2;
        }
        printf("{\"ok\":%d,\"delay\":%"PRId32",\"event\":[%u,%u,%u],\"ring\":[",ok,delay,event.kind,event.player,event.code);
        for(unsigned i=0;i<T3_REPLAY_SOUND_FRAMES;++i)printf("%s%"PRIu32,i?",":"",ring[i]);
        puts("]}");
    }
    return ferror(stdin)?2:0;
}
