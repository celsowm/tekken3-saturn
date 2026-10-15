/* Host-only numerical differential harness for the Tekken 3 PS1 solver.
 * Input: 49 little-endian signed 16-bit pose channels per frame.
 * Output: little-endian uint32 valid slot mask + 18 * 9 signed 16-bit
 * matrices for each frame. Invalid slots are ZEROED, not approximated.
 *
 * No original tables or poses are distributed; both are local files.
 */
#include "psx_ik.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { POSE_BYTES = T3_PSX_POSE_CHANNELS * 2,
       FRAME_BYTES = 4 + T3_PSX_ROTATION_SLOTS * 9 * 2,
       MAX_FRAMES = 100000 };

static int read_exact(FILE* in, uint8_t* p, size_t n) {
    return fread(p,1,n,in)==n && fgetc(in)==EOF;
}
static int load_blob(const char* name, uint8_t* buf, size_t n) {
    FILE* f=fopen(name,"rb");
    int ok;
    if (!f) {perror(name);return 0;}
    ok=read_exact(f,buf,n);
    if (fclose(f)!=0)ok=0;
    if(!ok)fprintf(stderr,"Invalid lookup blob size: %s (expected %lu)\n",
                   name,(unsigned long)n);
    return ok;
}
static uint16_t r16(const uint8_t* p) {
    return (uint16_t)(p[0]|((uint16_t)p[1]<<8u));
}
static int16_t s16(uint16_t n) {
    return (int16_t)(n<0x8000u?(int32_t)n:(int32_t)n-65536);
}
static void w16(uint8_t* p,uint16_t v) {
    p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8u);
}
static void w32(uint8_t* p,uint32_t v) {
    w16(p,(uint16_t)v);w16(p+2,(uint16_t)(v>>16u));
}
static int compare_path(const char* path, const uint8_t* trig,
                        const uint8_t* ik,const char* out_path) {
    FILE* in=NULL;
    FILE* out=NULL;
    t3_psx_ik_tables_t t={{trig,T3_PSX_TRIG_BYTES},ik,T3_PSX_IK_BYTES};
    uint8_t input[POSE_BYTES],buffer[FRAME_BYTES];
    int16_t pose[T3_PSX_POSE_CHANNELS];
    t3_psx_matrix_t matrices[T3_PSX_ROTATION_SLOTS];
    size_t bytes,n,frame;
    long size;
    unsigned i,j,k;
    int rc=1;
    in=fopen(path,"rb");
    if(!in){perror(path);return 0;}
    if(fseek(in,0,SEEK_END)!=0 || (size=ftell(in))<0 ||
       fseek(in,0,SEEK_SET)!=0 || size==0 ||
       (unsigned long)size>MAX_FRAMES*(unsigned long)POSE_BYTES ||
       size % POSE_BYTES) {
        fprintf(stderr,"Pose input must contain 1..%d complete 49-channel frames\n",MAX_FRAMES);
        fclose(in);return 0;
    }
    out=fopen(out_path,"wb");
    if(!out){perror(out_path);fclose(in);return 0;}
    n=(size_t)size/POSE_BYTES;
    for(frame=0;frame<n;++frame) {
        uint32_t mask=0;
        if(fread(input,1,POSE_BYTES,in)!=POSE_BYTES) {rc=0;break;}
        for(i=0;i<T3_PSX_POSE_CHANNELS;++i)pose[i]=s16(r16(input+2u*i));
        if(t3_psx_pose_full(pose,&t,matrices,&mask)!=1) {
            fprintf(stderr,"Unsupported or invalid PS1 solver pose at frame %lu\n",
                    (unsigned long)frame);
            rc=0;break;
        }
        memset(buffer,0,sizeof(buffer));
        w32(buffer,mask);
        for(i=0;i<T3_PSX_ROTATION_SLOTS;++i) {
            if(!(mask & (1u<<i)))continue;
            for(j=0;j<3u;++j)
                for(k=0;k<3u;++k)
                    w16(buffer+4u+2u*(i*9u+j*3u+k),
                        (uint16_t)matrices[i].m[j][k]);
        }
        bytes=fwrite(buffer,1,sizeof(buffer),out);
        if(bytes!=sizeof(buffer)){rc=0;break;}
    }
    if(fclose(in)!=0)rc=0;
    if(fclose(out)!=0)rc=0;
    if(!rc)remove(out_path); /* No partial/stale expected-output files. */
    return rc;
}
int main(int argc,char** argv) {
    uint8_t trig[T3_PSX_TRIG_BYTES],ik[T3_PSX_IK_BYTES];
    if(argc!=5) {
        fprintf(stderr,"usage: %s <trig.bin> <ik.bin> <poses.bin> <out.bin>\n",
                argv[0]);return 2;
    }
    if(!load_blob(argv[1],trig,sizeof(trig)) ||
       !load_blob(argv[2],ik,sizeof(ik)))return 2;
    return compare_path(argv[3],trig,ik,argv[4])?0:1;
}
