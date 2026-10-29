#include "psx_fighter_sounds.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
static int first;
static void emit(void* user,unsigned kind,unsigned player,uint16_t code) {
    (void)user;
    if(kind==T3_SOUND_VOICE_OFF)return; /* compared separately from replay events */
    printf("%s[%u,%u,%u]",first?"":",",kind,player,code);first=0;
}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    FILE* file=fopen(argv[1],"rb");if(!file)return 2;
    if(fseek(file,0,SEEK_END))return 2;
    const long size=ftell(file);if(size<24 || size>1048576)return 2;
    rewind(file);uint8_t* data=malloc((size_t)size);
    if(!data || fread(data,1,(size_t)size,file)!=(size_t)size)return 2;
    fclose(file);
    t3_psx_sound_tables_t tables={data,(size_t)size};
    if(!t3_psx_sound_tables_valid(tables))return 2;
    uint8_t bytes[256];size_t received;
    while((received=fread(bytes,1,sizeof(bytes),stdin))==sizeof(bytes)) {
        uint32_t words[64];
        for(unsigned k=0;k<64;++k)words[k]=(uint32_t)bytes[4*k]|((uint32_t)bytes[4*k+1]<<8)|
            ((uint32_t)bytes[4*k+2]<<16)|((uint32_t)bytes[4*k+3]<<24);
        uint32_t random=words[0];t3_psx_sound_state_t state[2];t3_psx_sound_facts_t facts[2];
        uint8_t commands[2][6];
        for(unsigned p=0;p<2;++p) {
            const uint32_t* s=words+4+7*p;
            state[p]=(t3_psx_sound_state_t){(int16_t)s[0],(int16_t)s[1],(int16_t)s[2],
                (int16_t)s[3],(int16_t)s[4],(int32_t)s[5],(uint8_t)s[6]};
            const uint32_t* f=words+18+23*p;
            for(unsigned k=0;k<3;++k){commands[p][2*k]=(uint8_t)f[20+k];commands[p][2*k+1]=(uint8_t)(f[20+k]>>8);}
            facts[p]=(t3_psx_sound_facts_t){.player=(uint8_t)f[0],.character=(uint8_t)f[1],
                .voice_set=(uint8_t)f[2],.first_joint=(uint8_t)f[3],.ko=(uint8_t)f[4],
                .hit_clean=(uint8_t)f[5],.got_hit=(uint8_t)f[6],.contact=(uint8_t)f[7],
                .guarded=(uint8_t)f[8],.move_changed=(uint8_t)f[9],.landed_a=(uint8_t)f[10],
                .landed_b=(uint8_t)f[11],.wooden=(uint8_t)f[12],.health=(int32_t)f[13],
                .damage=(int16_t)f[14],.throw_state=(int16_t)f[15],.pose_frame=(int16_t)f[16],
                .air_first=(uint8_t)f[17],.active_first=(uint8_t)f[18],
                .commands=commands[p],.command_bytes=f[19]*2};
        }
        t3_psx_sound_context_t context={tables,&random,words[1],words[2],words[3],emit,NULL};
        first=1;printf("{\"events\":[");
        int ok=t3_psx_fighter_sounds(state,facts,0,&context);
        printf("],\"ok\":%d,\"random\":%"PRIu32",\"state\":[",ok,random);
        for(unsigned p=0;p<2;++p) {
            const t3_psx_sound_state_t* s=&state[p];
            printf("%s[%d,%d,%d,%d,%d,%"PRId32",%u]",p?",":"",s->script,s->last_script,
                s->position,s->frame,s->voice_cooldown,s->damage_voice_timer,s->hold);
        }
        puts("]}");
    }
    free(data);return received || ferror(stdin) || !feof(stdin)?2:0;
}
