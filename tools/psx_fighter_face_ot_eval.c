#include "psx_fighter_ot.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char** argv) {
    if(argc==2 && (!strcmp(argv[1],"--projected") || !strcmp(argv[1],"--native") || !strcmp(argv[1],"--indexed"))) {
        int indexed=!strcmp(argv[1],"--indexed");
        int native=indexed || !strcmp(argv[1],"--native");
        unsigned group,flags,z[4];int x[4],y[4];
        while(scanf("%u %u %u %u %u %u %d %d %d %d %d %d %d %d",&group,&flags,
                &z[0],&z[1],&z[2],&z[3],&x[0],&y[0],&x[1],&y[1],&x[2],&y[2],&x[3],&y[3])==14) {
            if(group>3 || flags>255)return 2;
            t3_psx_projected_vertex_t v[4]={{0}};
            for(unsigned i=0;i<4;++i){if(z[i]>65535)return 2;v[i].z=(int16_t)z[i];}
            for(unsigned i=0;i<4;++i){if(x[i]< -1024 || x[i]>1023 || y[i]< -1024 || y[i]>1023)return 2;
                v[i].xy=(uint16_t)x[i]|((uint32_t)(uint16_t)y[i]<<16);}
            if(native && (group&1u)){t3_psx_projected_vertex_t swap=v[2];v[2]=v[3];v[3]=swap;}
            int32_t nclip;unsigned bucket;
            if(indexed) {
                t3_psx_projected_vertex_t scattered[7]={{0}};
                const uint16_t indices[3]={5,1,6};
                for(unsigned i=0;i<3;++i)scattered[indices[i]]=v[i];
                bucket=t3_psx_fighter_native_indexed_bucket(scattered,indices,(uint8_t)flags,&nclip);
            } else bucket=native?t3_psx_fighter_native_bucket(v,group,(uint8_t)flags,&nclip):
                t3_psx_fighter_projected_bucket(v,group,(uint8_t)flags,&nclip);
            printf("[%u,%"PRId32"]\n",bucket,nclip);
        }
        return ferror(stdin)||!feof(stdin)?2:0;
    }
    if(argc!=1)return 2;
    unsigned group,z[4];uint32_t w0,w1;int32_t nclip;
    while(scanf("%u %"SCNu32" %"SCNu32" %"SCNd32" %u %u %u %u",&group,&w0,&w1,&nclip,&z[0],&z[1],&z[2],&z[3])==8) {
        if(group>3 || z[0]>65535 || z[1]>65535 || z[2]>65535 || z[3]>65535)return 2;
        uint8_t slots[4]={(uint8_t)((w0>>2)&127),(uint8_t)((w0>>9)&127),(uint8_t)((w0>>16)&127),
            (uint8_t)((group&1)?(w0>>25)&127:(w0>>16)&127)};
        uint16_t depth[128]={0};for(unsigned i=0;i<4;++i)depth[slots[i]]=(uint16_t)z[i];
        t3_psx_fighter_face_ot_t result;
        if(!t3_psx_fighter_face_ot(&result,group,w0,w1,depth,nclip))return 2;
        printf("[%u,%u,%u,%u,%u,%"PRIu32"]\n",result.slots[0],result.slots[1],result.slots[2],
               result.slots[3],result.bucket,result.flags);
    }
    return ferror(stdin)||!feof(stdin)?2:0;
}
