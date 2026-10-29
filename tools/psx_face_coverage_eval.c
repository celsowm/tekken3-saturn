#include "psx_face_coverage.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char** argv) {
    if(argc==2 && (!strcmp(argv[1],"--triangle") || !strcmp(argv[1],"--parallelogram") || !strcmp(argv[1],"--quad") || !strcmp(argv[1],"--general"))) {
        int parallel=!strcmp(argv[1],"--parallelogram");
        int quad=!strcmp(argv[1],"--quad");
        int general=!strcmp(argv[1],"--general");
        int x[4],y[4];unsigned tx,ty,opaque;
        while(scanf("%d %d %d %d %d %d %d %d %u %u %u",&x[0],&y[0],&x[1],&y[1],
                    &x[2],&y[2],&x[3],&y[3],&tx,&ty,&opaque)==11) {
            uint8_t pixels[64],mask[8];int16_t vx[4],vy[4];
            for(unsigned i=0;i<4;++i){if(x[i]< -32768 || x[i]>32767 || y[i]< -32768 || y[i]>32767)return 2;vx[i]=(int16_t)x[i];vy[i]=(int16_t)y[i];}
            for(unsigned i=0;i<64;++i){unsigned v;if(scanf("%u",&v)!=1 || v>255)return 2;pixels[i]=(uint8_t)v;}
            int ok=general?t3_face_general_tile_coverage(pixels,(int)opaque,vx,vy,tx,ty,mask):
                   quad?t3_face_quad_tile_coverage(pixels,(int)opaque,vx,vy,tx,ty,mask):
                   parallel?t3_face_parallelogram_tile_coverage(pixels,(int)opaque,vx,vy,tx,ty,mask):
                            t3_face_triangle_tile_coverage(pixels,(int)opaque,vx,vy,tx,ty,mask);
            if(!ok)return 2;
            putchar('[');for(unsigned i=0;i<8;++i)printf("%s%u",i?",":"",mask[i]);puts("]");
        }
        return ferror(stdin)||!feof(stdin)?2:0;
    }
    if(argc==2 && !strcmp(argv[1],"--rect")) {
        int left,top,right,bottom;unsigned tx,ty,opaque;
        while(scanf("%d %d %d %d %u %u %u",&left,&top,&right,&bottom,&tx,&ty,&opaque)==7) {
            uint8_t pixels[64],mask[8];
            for(unsigned i=0;i<64;++i){unsigned v;if(scanf("%u",&v)!=1 || v>255)return 2;pixels[i]=(uint8_t)v;}
            if(!t3_face_rect_tile_coverage(pixels,(int)opaque,left,top,right,bottom,tx,ty,mask))return 2;
            putchar('[');for(unsigned i=0;i<8;++i)printf("%s%u",i?",":"",mask[i]);puts("]");
        }
        return ferror(stdin)||!feof(stdin)?2:0;
    }
    if(argc!=1)return 2;
    unsigned opaque;
    while(scanf("%u",&opaque)==1) {
        uint8_t pixels[64],mask[8];
        for(unsigned i=0;i<64;++i){unsigned v;if(scanf("%u",&v)!=1 || v>255)return 2;pixels[i]=(uint8_t)v;}
        if(!t3_face_index8_coverage(pixels,(int)opaque,mask))return 2;
        putchar('[');for(unsigned i=0;i<8;++i)printf("%s%u",i?",":"",mask[i]);puts("]");
    }
    return ferror(stdin)||!feof(stdin)?2:0;
}
