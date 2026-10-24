/* Runs the backdrop line mapping on cameras given on stdin, for
 * scripts/check_backdrop_mapping.py. No game data: the numbers come from the
 * caller.
 *
 *   first line:  box_half bake_height tan_top rows_per_tan columns rows width height step
 *   each camera: turn eye0 eye1 eye2 view_proj0 .. view_proj15
 *   output:      per camera one line, "ok" then x y zoom wall_depth per line
 *                group, or "fail" */
#include "stage_backdrop.h"
#include <stdio.h>

int main(void) {
    t3_stage_backdrop_t b;
    unsigned columns,rows,width,height,step;
    static t3_stage_backdrop_line_t lines[1024];
    static int32_t depth[1024];
    if(scanf("%d %d %d %d %u %u %u %u %u",&b.box_half,&b.bake_height,&b.tan_top,&b.rows_per_tan,
             &columns,&rows,&width,&height,&step)!=9 || !step || height/step>1024)return 1;
    b.columns=(uint16_t)columns;b.rows=(uint16_t)rows;b.width=(uint16_t)width;b.height=(uint16_t)height;
    for(;;) {
        int32_t turn,eye[3],m[16];
        if(scanf("%d %d %d %d",&turn,&eye[0],&eye[1],&eye[2])!=4)break;
        for(int i=0;i<16;++i)if(scanf("%d",&m[i])!=1)return 1;
        if(!t3_stage_backdrop_lines(&b,m,eye,turn,step,lines,depth)){puts("fail");continue;}
        fputs("ok",stdout);
        for(unsigned n=0;n<height/step;++n)
            printf(" %d %d %u %d",lines[n].x,lines[n].y,lines[n].zoom,depth[n]);
        putchar('\n');
    }
    return 0;
}
