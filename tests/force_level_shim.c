/* ctypes entry for scripts/check_force_flow_trace.py: one level-runner step on the flat vector. */
#include "psx_force_level.h"
#include <string.h>
void force_step(const int32_t* in,int32_t* out,const uint8_t* tables,const uint8_t* script,int32_t* events){
    t3_force_level_t s;
    memset(&s,0,sizeof s);
    t3_force_level_load(&s,in);
    for(int i=0;i<5;++i)s.countdown_sound[i]=0;
    t3_force_level_step(&s,tables,script);
    t3_force_level_save(&s,out);
    events[0]=(int32_t)s.event_count;
    for(unsigned i=0;i<s.event_count;++i){events[1+4*i]=s.events[i].kind;events[2+4*i]=s.events[i].a;events[3+4*i]=s.events[i].b;events[4+4*i]=s.events[i].c;}
}
