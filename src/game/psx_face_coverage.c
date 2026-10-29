/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Source texture transparency follows Tekken3Remake 3dmk-models research.
 * See NOTICE.md. */
#include "psx_face_coverage.h"
int t3_face_index8_coverage(const uint8_t pixels[64],int zero_is_opaque,uint8_t mask[8]) {
    if(!pixels || !mask || (zero_is_opaque!=0 && zero_is_opaque!=1))return 0;
    uint8_t rows[8];
    for(unsigned y=0;y<8;++y) {
        unsigned bits=0;
        for(unsigned x=0;x<8;++x)bits=(bits<<1)|(zero_is_opaque || pixels[8*y+x]!=0);
        rows[y]=(uint8_t)bits;
    }
    for(unsigned y=0;y<8;++y)mask[y]=rows[y];
    return 1;
}
static unsigned rectangle_sample(int pos,int span) {
    /* Reduced 8-texel sprites sample destination-pixel centers. Enlarged
     * sprites use endpoint interpolation with ties toward the earlier texel. */
    if(span<7)return (unsigned)((2*pos+1)*4/(span+1));
    return (unsigned)((pos*7+(span-1)/2)/span);
}
int t3_face_rect_tile_coverage(const uint8_t pixels[64],int zero_is_opaque,
    int left,int top,int right,int bottom,unsigned tile_x,unsigned tile_y,uint8_t mask[8]) {
    if(!pixels || !mask || (zero_is_opaque!=0 && zero_is_opaque!=1) ||
       left< -1024 || top< -1024 || right>1023 || bottom>1023 ||
       right<left || bottom<top || tile_x>=40 || tile_y>=28)return 0;
    const int width=right-left,height=bottom-top;uint8_t rows[8];
    for(unsigned y=0;y<8;++y) {
        unsigned bits=0;int py=(int)tile_y*8+(int)y;
        for(unsigned x=0;x<8;++x) {
            int px=(int)tile_x*8+(int)x;unsigned opaque=0;
            if(px>=left && px<=right && py>=top && py<=bottom) {
                unsigned u=rectangle_sample(px-left,width);
                unsigned v=rectangle_sample(py-top,height);
                opaque=zero_is_opaque || pixels[v*8+u]!=0;
            }
            bits=(bits<<1)|opaque;
        }
        rows[y]=(uint8_t)bits;
    }
    for(unsigned y=0;y<8;++y)mask[y]=rows[y];
    return 1;
}
static int triangle_edge(int from,int to,int step,int height) {
    if(!height)return from;
    int delta=to-from,sign=delta<0?-1:1;if(delta<0)delta=-delta;
    return from+sign*((delta*step+(height-1)/2)/height);
}
int t3_face_triangle_tile_coverage(const uint8_t pixels[64],int zero_is_opaque,
    const int16_t x[4],const int16_t y[4],unsigned tile_x,unsigned tile_y,uint8_t mask[8]) {
    if(!pixels || !x || !y || !mask || (zero_is_opaque!=0 && zero_is_opaque!=1) ||
       tile_x>=40 || tile_y>=28 || y[0]!=y[1] || x[2]!=x[3] || y[2]!=y[3] ||
       x[0]>x[1] || y[2]<y[0])return 0;
    for(unsigned i=0;i<4;++i)if(x[i]< -1024 || x[i]>1023 || y[i]< -1024 || y[i]>1023)return 0;
    int height=y[2]-y[0],left_delta=x[2]-x[0],right_delta=x[2]-x[1];
    if(left_delta>height || left_delta< -height || right_delta>height || right_delta< -height)return 0;
    uint8_t rows[8];
    for(unsigned iy=0;iy<8;++iy) {
        int py=(int)tile_y*8+(int)iy,step=py-y[0];unsigned bits=0;
        if(step<0 || step>height){rows[iy]=0;continue;}
        int left=triangle_edge(x[0],x[2],step,height),right=triangle_edge(x[1],x[2],step,height);
        unsigned v=rectangle_sample(step,height);
        for(unsigned ix=0;ix<8;++ix) {
            int px=(int)tile_x*8+(int)ix;unsigned opaque=0;
            if(px>=left && px<=right) {
                unsigned u=rectangle_sample(px-left,right-left);
                opaque=zero_is_opaque || pixels[v*8+u]!=0;
            }
            bits=(bits<<1)|opaque;
        }
        rows[iy]=(uint8_t)bits;
    }
    for(unsigned i=0;i<8;++i)mask[i]=rows[i];
    return 1;
}
static void coverage_put(uint8_t rows[8],int x,int y,int tile_left,int tile_top) {
    int px=x-tile_left,py=y-tile_top;
    if(px>=0 && px<8 && py>=0 && py<8)rows[py]|=(uint8_t)(0x80u>>px);
}
int t3_face_parallelogram_tile_coverage(const uint8_t pixels[64],int zero_is_opaque,
    const int16_t x[4],const int16_t y[4],unsigned tile_x,unsigned tile_y,uint8_t mask[8]) {
    if(!pixels || !x || !y || !mask || (zero_is_opaque!=0 && zero_is_opaque!=1) ||
       tile_x>=40 || tile_y>=28)return 0;
    for(unsigned i=0;i<4;++i)if(x[i]< -1024 || x[i]>1023 || y[i]< -1024 || y[i]>1023)return 0;
    int across=x[1]-x[0],across_y=y[1]-y[0],edge_x=x[3]-x[0],edge_y=y[3]-y[0];
    if(x[2]-x[1]!=edge_x || y[2]-y[1]!=edge_y || across<0 || across>64 ||
       across_y<0 || across_y>across || edge_y<0 || edge_y>64 ||
       edge_x>(edge_y<0?-edge_y:edge_y) || edge_x< -(edge_y<0?-edge_y:edge_y))return 0;
    int steps=edge_y<0?-edge_y:edge_y,tile_left=(int)tile_x*8,tile_top=(int)tile_y*8;
    uint8_t rows[8]={0};
    for(int i=0;i<=steps;++i) {
        int lx=triangle_edge(x[0],x[3],i,steps),ly=triangle_edge(y[0],y[3],i,steps);
        int ry=ly+across_y,prev_y=ly;unsigned v=rectangle_sample(i,steps);
        for(int j=0;j<=across;++j) {
            int px=lx+j,py=triangle_edge(ly,ry,j,across);unsigned u=rectangle_sample(j,across);
            if(zero_is_opaque || pixels[v*8+u]) {
                if(j && py!=prev_y)coverage_put(rows,px,prev_y,tile_left,tile_top);
                coverage_put(rows,px,py,tile_left,tile_top);
            }
            prev_y=py;
        }
    }
    for(unsigned i=0;i<8;++i)mask[i]=rows[i];
    return 1;
}
static int abs_i(int v){return v<0?-v:v;}
static int scaled_step(int i,int span,int total) {
    return total?(i*span+(total-1)/2)/total:0;
}
int t3_face_quad_supported(const int16_t x[4],const int16_t y[4]) {
    if(!x || !y)return 0;
    for(unsigned i=0;i<4;++i)if(x[i]< -1024 || x[i]>1023 || y[i]< -1024 || y[i]>1023)return 0;
    /* The general forward-line model disagrees with captured rotated
     * triangles even when its major-axis checks pass. Keep those rejected
     * until their coverage and UV interpolation are independently fixed. */
    if(x[2]==x[3] && y[2]==y[3] && y[0]!=y[1])return 0;
    int left_height=y[3]-y[0],right_height=y[2]-y[1];
    if(left_height<0 || right_height<0 || left_height>64 || right_height>64 ||
       abs_i(x[3]-x[0])>left_height || abs_i(x[2]-x[1])>right_height)return 0;
    int steps=left_height>right_height?left_height:right_height;
    /* The two nested rounded interpolations can each advance by at most one
     * per outer step. Keep their remainders instead of dividing four times
     * per scanline; the initial half-down biases match scaled_step/triangle_edge. */
    int left_pos=0,right_pos=0,left_x=x[0],right_x=x[1];
    int left_phase=steps?(steps-1)/2:0,right_phase=left_phase;
    int left_dx=abs_i(x[3]-x[0]),right_dx=abs_i(x[2]-x[1]);
    int left_error=left_height?(left_height-1)/2:0,right_error=right_height?(right_height-1)/2:0;
    int left_sign=x[3]<x[0]?-1:1,right_sign=x[2]<x[1]?-1:1;
    for(int i=0;i<=steps;++i) {
        int width=right_x-left_x;
        if(width<0 || width>64 || abs_i((y[1]+right_pos)-(y[0]+left_pos))>width)return 0;
        if(i==steps)break;
        left_phase+=left_height;
        if(left_phase>=steps) {
            left_phase-=steps;++left_pos;left_error+=left_dx;
            if(left_error>=left_height){left_error-=left_height;left_x+=left_sign;}
        }
        right_phase+=right_height;
        if(right_phase>=steps) {
            right_phase-=steps;++right_pos;right_error+=right_dx;
            if(right_error>=right_height){right_error-=right_height;right_x+=right_sign;}
        }
    }
    return 1;
}
int t3_face_quad_tile_coverage(const uint8_t pixels[64],int zero_is_opaque,
    const int16_t x[4],const int16_t y[4],unsigned tile_x,unsigned tile_y,uint8_t mask[8]) {
    if(!pixels || !x || !y || !mask || (zero_is_opaque!=0 && zero_is_opaque!=1) ||
       tile_x>=40 || tile_y>=28 || !t3_face_quad_supported(x,y))return 0;
    int left_height=y[3]-y[0],right_height=y[2]-y[1];
    int steps=left_height>right_height?left_height:right_height;
    int tile_left=(int)tile_x*8,tile_top=(int)tile_y*8;uint8_t rows[8]={0};
    for(int i=0;i<=steps;++i) {
        int a=scaled_step(i,left_height,steps),b=scaled_step(i,right_height,steps);
        int lx=triangle_edge(x[0],x[3],a,left_height),ly=y[0]+a;
        int rx=triangle_edge(x[1],x[2],b,right_height),ry=y[1]+b;
        int width=rx-lx;
        int previous_x=lx,previous_y=ly;unsigned v=rectangle_sample(i,steps);
        for(int j=0;j<=width;++j) {
            int px=lx+j,py=triangle_edge(ly,ry,j,width);unsigned u=rectangle_sample(j,width);
            if(zero_is_opaque || pixels[v*8+u]) {
                if(j && py!=previous_y) {
                    if(py>previous_y)coverage_put(rows,px,previous_y,tile_left,tile_top);
                    else coverage_put(rows,previous_x,py,tile_left,tile_top);
                }
                coverage_put(rows,px,py,tile_left,tile_top);
            }
            previous_x=px;previous_y=py;
        }
    }
    for(unsigned i=0;i<8;++i)mask[i]=rows[i];
    return 1;
}
typedef struct { int x,y,dx,dy,span,phase,xerr,yerr; } general_edge_t;
static void general_edge_step(general_edge_t* edge,int steps) {
    edge->phase+=edge->span;
    if(edge->phase<steps)return;
    edge->phase-=steps;
    edge->xerr+=abs_i(edge->dx);
    if(edge->xerr>=edge->span){edge->xerr-=edge->span;edge->x+=edge->dx<0?-1:1;}
    edge->yerr+=abs_i(edge->dy);
    if(edge->yerr>=edge->span){edge->yerr-=edge->span;edge->y+=edge->dy<0?-1:1;}
}
int t3_face_general_tile_coverage(const uint8_t pixels[64],int zero_is_opaque,
    const int16_t x[4],const int16_t y[4],unsigned tile_x,unsigned tile_y,uint8_t mask[8]) {
    if(!pixels || !x || !y || !mask || (zero_is_opaque!=0 && zero_is_opaque!=1) ||
       tile_x>=40 || tile_y>=28)return 0;
    for(unsigned k=0;k<4;++k)if(x[k]<-1024 || x[k]>1023 || y[k]<-1024 || y[k]>1023)return 0;
    int lx=x[3]-x[0],ly=y[3]-y[0],rx=x[2]-x[1],ry=y[2]-y[1];
    int lspan=abs_i(lx)>abs_i(ly)?abs_i(lx):abs_i(ly);
    int rspan=abs_i(rx)>abs_i(ry)?abs_i(rx):abs_i(ry);
    int steps=lspan>rspan?lspan:rspan;
    if(steps>64)return 0;
    /* Cross-lines are convex combinations of AB and DC. */
    for(unsigned k=0;k<2;++k) {
        unsigned a=k?3:0,b=k?2:1;
        if(abs_i(x[b]-x[a])>64 || abs_i(y[b]-y[a])>64)return 0;
    }
    int tile_left=(int)tile_x*8,tile_top=(int)tile_y*8;uint8_t rows[8]={0};
    int edge_phase=steps?(steps-1)/2:0;
    general_edge_t left={x[0],y[0],lx,ly,lspan,edge_phase,
        lspan?(lspan-1+(ly<0))/2:0,lspan?(lspan-1+(lx<0))/2:0};
    general_edge_t right={x[1],y[1],rx,ry,rspan,edge_phase,
        rspan?(rspan-1+(ry<0))/2:0,rspan?(rspan-1+(rx<0))/2:0};
    const unsigned vden=steps<7?(unsigned)steps+1u:(unsigned)steps;
    const unsigned vstep=steps<7?8u:7u;
    unsigned verr=steps<7?4u:(unsigned)(steps-1)/2u,v=0;
    while(verr>=vden){verr-=vden;++v;}
    for(int i=0;i<=steps;++i) {
        int x1=left.x,y1=left.y,x2=right.x,y2=right.y;
#if !defined(T3_GENERAL_MASK_NO_LINE_CLIP)
        /* Both the line and its diagonal fill pixels stay inside the
         * endpoints' rectangle, so a disjoint line cannot affect this tile. */
        if((x1<tile_left && x2<tile_left) || (x1>tile_left+7 && x2>tile_left+7) ||
           (y1<tile_top && y2<tile_top) || (y1>tile_top+7 && y2>tile_top+7))goto next_line;
#endif
        int dx=x2-x1,dy=y2-y1,span=abs_i(dx)>abs_i(dy)?abs_i(dx):abs_i(dy);
        int px=x1,py=y1,xx=x1,yy=y1;
        const int xinc=dx<0?-1:1,yinc=dy<0?-1:1,adx=abs_i(dx),ady=abs_i(dy);
        int xerr=span?(span-1)/2:0,yerr=xerr;
        const unsigned uden=span<7?(unsigned)span+1u:(unsigned)span;
        const unsigned ustep=span<7?8u:7u;
        unsigned uerr=span<7?4u:(unsigned)(span-1)/2u,u=0;
        while(uerr>=uden){uerr-=uden;++u;}
        for(int j=0;j<=span;++j) {
            if(zero_is_opaque || pixels[v*8+u]) {
                if(j && xx!=px && yy!=py) {
                    if((dx>=0)==(dy>=0))coverage_put(rows,xx,py,tile_left,tile_top);
                    else coverage_put(rows,px,yy,tile_left,tile_top);
                }
                coverage_put(rows,xx,yy,tile_left,tile_top);
            }
            px=xx;py=yy;
            if(j==span)break;
            xerr+=adx;if(xerr>=span){xerr-=span;xx+=xinc;}
            yerr+=ady;if(yerr>=span){yerr-=span;yy+=yinc;}
            uerr+=ustep;while(uerr>=uden){uerr-=uden;++u;}
        }
#if !defined(T3_GENERAL_MASK_NO_LINE_CLIP)
next_line:
#endif
        if(i==steps)break;
        general_edge_step(&left,steps);general_edge_step(&right,steps);
        verr+=vstep;while(verr>=vden){verr-=vden;++v;}
    }
    for(unsigned i=0;i<8;++i)mask[i]=rows[i];
    return 1;
}
