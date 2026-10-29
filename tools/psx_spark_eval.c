#include "psx_effect_pool.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    FILE* f=fopen(argv[1],"rb");uint8_t table[T3_PSX_TRIG_BYTES];
    if(!f || fseek(f,0x800+0xE8C4,SEEK_SET) || fread(table,1,sizeof table,f)!=sizeof table)return 2;
    fclose(f);const t3_psx_trig_t trig={table,sizeof table};
    uint32_t seed;unsigned steps,buffer,type,flags,state,model,clut0,clut1;
    int pitch,yaw,speed;int32_t x,y,z,vx,vy,vz,a,b,c,d;
    while(scanf("%"SCNu32" %u %u %u %u %u %u %d %d %d %u %u %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32,
        &seed,&steps,&buffer,&type,&flags,&state,&model,&pitch,&yaw,&speed,&clut0,&clut1,
        &x,&y,&z,&vx,&vy,&vz,&a,&b,&c,&d)==22) {
        t3_psx_spark_t s={(uint8_t)type,(uint8_t)flags,(uint8_t)state,(uint8_t)model,
            (int16_t)pitch,(int16_t)yaw,(int16_t)speed,{(uint16_t)clut0,(uint16_t)clut1},x,y,z,vx,vy,vz,a,b,c,d};
        if(type==4 || type==5) {
            t3_psx_effect_pool_t pool={0};t3_psx_effect_actor_t actors[2]={{0}};
            t3_psx_effect_pool_round(&pool);t3_psx_spark_t* o=t3_psx_effect_alloc(&pool,type);*o=s;
            for(unsigned n=0;n<steps && o->type;++n)
                if(!t3_psx_effect_pool_step(&pool,&trig,NULL,0,actors,&seed,(buffer+n)&1u,1,0,2))return 2;
            s=*o;
        } else for(unsigned n=0;n<steps && s.type;++n) {
            if(!t3_psx_spark_step(&s,&trig,&seed,(buffer+n)&1u))return 2;
        }
        printf("[ %"PRIu32",%u,%u,%u,%u,%d,%d,%d,%u,%u,%"PRId32",%"PRId32",%"PRId32",%"PRId32",%"PRId32",%"PRId32",%"PRId32",%"PRId32",%"PRId32",%"PRId32" ]\n",
            seed,s.type,s.flags,s.state,s.model,s.pitch,s.yaw,s.speed,s.clut[0],s.clut[1],s.x,s.y,s.z,s.vx,s.vy,s.vz,s.a,s.b,s.c,s.d);
    }
    return ferror(stdin) || !feof(stdin)?2:0;
}
