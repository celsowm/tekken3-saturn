/* Digest of every backdrop line word over a camera sweep: the W6.3 bit-exact
 * A/B tool for stage_backdrop.c. Prints one FNV-1a digest for each entry point;
 * the optimisation is exact only when both digests and the seen counts are
 * unchanged. Host-only, no game data. */
#include "stage_backdrop.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define ONE 65536.0
#define PI 3.14159265358979323846

static const t3_stage_backdrop_t B={9608,1100,52429,13107200,1024,240,320,224};
static unsigned long long g_hash=1469598103934665603ull;
static unsigned long long g_words;

static void mix(int32_t v) {
    uint32_t u=(uint32_t)v;
    for(int i=0;i<4;++i) {
        g_hash^=(unsigned char)(u>>(8*i));
        g_hash*=1099511628211ull;
    }
    ++g_words;
}

static void cross(const double a[3],const double b[3],double o[3]) {
    o[0]=a[1]*b[2]-a[2]*b[1];o[1]=a[2]*b[0]-a[0]*b[2];o[2]=a[0]*b[1]-a[1]*b[0];
}

/* One camera; returns 1 when the mapping accepted it. */
static int digest(double yaw_deg,double pitch_deg,double ex,double height,double ez,int turn,
                  unsigned step,int vertical_offset,int second_entry) {
    const double yaw=yaw_deg*PI/180,pitch=pitch_deg*PI/180;
    const double fx=500.0/184,fy=0x1BD2*500.0/(240*4096)*(vertical_offset?15.0/14.0:1.0);
    double forward[3]={sin(-yaw)*cos(pitch),sin(pitch),-cos(-yaw)*cos(pitch)},right[3],up[3];
    const double world_up[3]={0,1,0},eye[3]={ex*96,height*96,-ez*96};
    static t3_stage_backdrop_line_t lines[224];
    static int32_t depth[224];
    int32_t m[16]={0},e[3];
    cross(forward,world_up,right);
    {
        const double n=sqrt(right[0]*right[0]+right[1]*right[1]+right[2]*right[2]);
        for(int i=0;i<3;++i)right[i]/=n;
    }
    cross(right,forward,up);
    for(int i=0;i<3;++i) {
        m[i]=(int32_t)lround(fx*right[i]*ONE);m[4+i]=(int32_t)lround(fy*up[i]*ONE);
        m[12+i]=(int32_t)lround(forward[i]*ONE);e[i]=(int32_t)lround(eye[i]);
    }
    int ok=second_entry?t3_stage_backdrop_lines(&B,m,e,turn,step,lines,depth)
                       :t3_stage_backdrop_lines_offset(&B,m,e,turn,step,vertical_offset,lines,depth);
    if(!ok)return 0;
    for(unsigned n=0;n<B.height/step;++n) {
        mix(lines[n].x);mix(lines[n].y);mix(lines[n].zoom);
        if(depth)mix(depth[n]);
    }
    return 1;
}

int main(void) {
    long seen=0,calls=0;
    for(int yi=-180;yi<180;yi+=15)
    for(int pi=-10;pi<=10;pi+=5)
    for(int xi=-1;xi<=1;++xi)
    for(int zi=-1;zi<=1;++zi)
    for(int hi=0;hi<3;++hi)
    for(int ti=0;ti<4;++ti)
    for(unsigned si=0;si<3;++si)
    for(int vi=-1;vi<=1;++vi) {
        const unsigned step=(unsigned[]){1u,2u,4u}[si];
        const int vo=vi;
        static const double heights[3]={64,96,136};
        calls+=2;
        seen+=digest(yi,pi,xi*150,heights[hi],zi*150,ti*1024,step,vo,0);
        seen+=digest(yi,pi,xi*150,heights[hi],zi*150,ti*1024,step,vo,1);
    }
    printf("calls %ld seen %ld words %llu digest %016llx\n",calls,seen,g_words,g_hash);
    return 0;
}
