#include "psx_face_coverage.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int absolute(int x){return x<0?-x:x;}
static int reference_edge(int a,int b,int i,int span) {
    if(!span)return a;
    int d=b-a,s=d<0?-1:1;if(d<0)d=-d;
    return a+s*((d*i+(span-1)/2)/span);
}
static int reference(const int16_t x[4],const int16_t y[4]) {
    for(unsigned i=0;i<4;++i)if(x[i]< -1024 || x[i]>1023 || y[i]< -1024 || y[i]>1023)return 0;
    if(x[2]==x[3] && y[2]==y[3] && y[0]!=y[1])return 0;
    int lh=y[3]-y[0],rh=y[2]-y[1];
    if(lh<0 || rh<0 || lh>64 || rh>64 || absolute(x[3]-x[0])>lh || absolute(x[2]-x[1])>rh)return 0;
    int n=lh>rh?lh:rh;
    for(int i=0;i<=n;++i) {
        int a=n?(i*lh+(n-1)/2)/n:0,b=n?(i*rh+(n-1)/2)/n:0;
        int lx=reference_edge(x[0],x[3],a,lh),rx=reference_edge(x[1],x[2],b,rh);
        int width=rx-lx;if(width<0 || width>64 || absolute(y[1]+b-y[0]-a)>width)return 0;
    }
    return 1;
}
static uint32_t rng=0x836b54acu;
static unsigned roll(unsigned limit){rng=rng*1664525u+1013904223u;return (rng>>8)%limit;}
static unsigned sample(int p,int span) {
    return span<7?(unsigned)((2*p+1)*4/(span+1)):(unsigned)((p*7+(span-1)/2)/span);
}
static void put(uint8_t out[8],int x,int y,int tx,int ty) {
    x-=tx*8;y-=ty*8;
    if(x>=0 && x<8 && y>=0 && y<8)out[y]|=(uint8_t)(0x80u>>x);
}
/* Full un-clipped scan of the accepted quad, including diagonal fill. */
static void coverage_reference(const uint8_t pixels[64],const int16_t x[4],const int16_t y[4],
                               int tx,int ty,uint8_t out[8]) {
    memset(out,0,8);
    int lh=y[3]-y[0],rh=y[2]-y[1],steps=lh>rh?lh:rh;
    for(int i=0;i<=steps;++i) {
        int a=steps?(i*lh+(steps-1)/2)/steps:0,b=steps?(i*rh+(steps-1)/2)/steps:0;
        int lx=reference_edge(x[0],x[3],a,lh),ly=y[0]+a;
        int rx=reference_edge(x[1],x[2],b,rh),ry=y[1]+b,width=rx-lx;
        int px=lx,py=ly;unsigned v=sample(i,steps);
        for(int j=0;j<=width;++j) {
            int nx=lx+j,ny=reference_edge(ly,ry,j,width);
            if(pixels[v*8+sample(j,width)]) {
                if(j && ny!=py) {
                    if(ny>py)put(out,nx,py,tx,ty);else put(out,px,ny,tx,ty);
                }
                put(out,nx,ny,tx,ty);
            }
            px=nx;py=ny;
        }
    }
}
static void check_clipped_coverage(void) {
    unsigned checked=0;
    for(unsigned trial=0;trial<12000;++trial) {
        int16_t x[4],y[4];
        x[0]=(int16_t)((int)roll(360)-20);y[0]=(int16_t)((int)roll(264)-20);
        x[1]=(int16_t)(x[0]+roll(50));y[1]=(int16_t)(y[0]+(int)roll(13)-6);
        int lh=(int)roll(50),rh=(int)roll(50);
        y[3]=(int16_t)(y[0]+lh);y[2]=(int16_t)(y[1]+rh);
        x[3]=(int16_t)(x[0]+(int)roll((unsigned)(2*lh+1))-lh);
        x[2]=(int16_t)(x[1]+(int)roll((unsigned)(2*rh+1))-rh);
        if(!reference(x,y))continue;
        uint8_t pixels[64],expected[8],actual[8];
        for(unsigned i=0;i<64;++i)pixels[i]=(uint8_t)roll(3);
        for(unsigned i=0;i<8;++i) {
            unsigned tx=roll(40),ty=roll(28);
            if(i<4) {tx=(unsigned)((x[i]<0?0:x[i]>319?319:x[i])/8);ty=(unsigned)((y[i]<0?0:y[i]>223?223:y[i])/8);}
            coverage_reference(pixels,x,y,(int)tx,(int)ty,expected);
            assert(t3_face_quad_tile_coverage(pixels,0,x,y,tx,ty,actual));
            assert(!memcmp(expected,actual,8));++checked;
        }
    }
    assert(checked>5000);
}
int main(void) {
    check_clipped_coverage();
    assert(!t3_face_quad_supported(NULL,NULL));
    const int16_t tx[4]={30,36,33,33},ty[4]={22,18,29,29};
    uint8_t pixels[64]={0},mask[8];
    for(unsigned i=0;i<8;++i)mask[i]=0x5a;
    assert(!t3_face_quad_supported(tx,ty));
    assert(!t3_face_quad_tile_coverage(pixels,0,tx,ty,4,3,mask));
    for(unsigned i=0;i<8;++i)assert(mask[i]==0x5a);
    unsigned yes=0,no=0;
    for(unsigned i=0;i<400000;++i) {
        int16_t x[4],y[4];
        if(i&1u) {
            x[0]=(int16_t)((int)roll(1500)-750);y[0]=(int16_t)((int)roll(1500)-750);
            x[1]=(int16_t)(x[0]+(int)roll(81)-8);y[1]=(int16_t)(y[0]+(int)roll(41)-20);
            int lh=(int)roll(65),rh=(int)roll(65);
            y[3]=(int16_t)(y[0]+lh);y[2]=(int16_t)(y[1]+rh);
            x[3]=(int16_t)(x[0]+(int)roll((unsigned)(2*lh+1))-lh);
            x[2]=(int16_t)(x[1]+(int)roll((unsigned)(2*rh+1))-rh);
        } else for(unsigned k=0;k<4;++k){x[k]=(int16_t)((int)roll(2200)-1100);y[k]=(int16_t)((int)roll(2200)-1100);}
        int expected=reference(x,y),actual=t3_face_quad_supported(x,y);
        assert(expected==actual);if(actual)++yes;else ++no;
    }
    assert(yes>10000 && no>10000);
    printf("400000 preflight cases, %u accepted and %u rejected\n",yes,no);
    return 0;
}
