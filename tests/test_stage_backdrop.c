/* The integer backdrop line mapping against a double-precision reference of
 * the same rule (stage_backdrop.h), on synthetic cameras. No game data. */
#include "stage_backdrop.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>

#define ONE 65536.0
#define PI 3.14159265358979323846

static const t3_stage_backdrop_t B={9608,1100,52429,13107200,1024,240,320,224};

typedef struct { double column,row,increment,depth; int seen; } ref_t;

/* The wall hit of a PS1 local ray: depth along it, or 0. */
static double wall(const double e[3],const double d[3],double p[3]) {
    double t=-1;
    for(int axis=0;axis<3;axis+=2) {
        double s;
        if(fabs(d[axis])<64/ONE)continue;
        s=((d[axis]>0?B.box_half:-B.box_half)-e[axis])/d[axis];
        if(s>0 && (t<0 || s<t))t=s;
    }
    if(t<=0)return 0;
    for(int i=0;i<3;++i)p[i]=e[i]+t*d[i];
    return t;
}

static void picture(const double p[3],double* column,double* row) {
    const double turns=atan2(p[0],p[2])/(2*PI);
    *column=(turns<0?turns+1:turns)*B.columns;
    *row=(B.tan_top/ONE-(-p[1]-B.bake_height)/hypot(p[0],p[2]))*(B.rows_per_tan/ONE);
}

/* The reference for screen line `y` (dot index) of a camera given by unit
 * axes (Saturn world), focal terms, eye (Saturn fx16 values) and turn. */
static ref_t reference(const double right[3],const double up[3],const double forward[3],double fx,double fy,
                       const double eye[3],int turn,double y) {
    /* Five rays at dots (2k+1) * width / 10: least-squares column line, mean row, nearest wall. */
    const double a=(B.height-2*y)/B.height/fy;
    const double c=cos(2*PI*turn/4096),s=sin(2*PI*turn/4096);
    const double x=eye[0]/96,z=-eye[2]/96;
    const double e[3]={x*c+z*s,-eye[1]/96,-x*s+z*c};
    double p[3],column[5],row,slope=0,spread=0;
    ref_t r={0};
    for(int k=0;k<5;++k) {
        const double offset=(2*k-4)*B.width/10.0,across=2*offset/B.width/fx;
        double w[3],d[3],t;
        for(int i=0;i<3;++i)w[i]=forward[i]+a*up[i]+across*right[i];
        d[0]=w[0]*c-w[2]*s;d[1]=-w[1];d[2]=-w[0]*s-w[2]*c;
        if(!(t=wall(e,d,p)))return r;
        if(!k || t<r.depth)r.depth=t;
        picture(p,&column[k],&row);
        if(k)column[k]=column[k-1]+fmod(column[k]-column[k-1]+1.5*B.columns,B.columns)-0.5*B.columns;
        r.column+=column[k]/5;r.row+=row/5;
        slope+=offset*column[k];spread+=offset*offset;
    }
    r.column=fmod(r.column+B.columns,B.columns);
    r.increment=slope/spread;r.seen=1;
    return r;
}

static void cross(const double a[3],const double b[3],double o[3]) {
    o[0]=a[1]*b[2]-a[2]*b[1];o[1]=a[2]*b[0]-a[0]*b[2];o[2]=a[0]*b[1]-a[1]*b[0];
}

/* A camera at `eye` (PS1 units around the midpoint, y up) looking along PS1
 * yaw `yaw` (forward (sin -yaw, cos -yaw)) pitched up by `pitch` degrees. */
static void check(double yaw_deg,double pitch_deg,double ex,double height,double ez,int turn,unsigned step) {
    const double yaw=yaw_deg*PI/180,pitch=pitch_deg*PI/180;
    const double fx=500.0/184,fy=0x1BD2*500.0/(240*4096);
    /* PS1 forward (sin -yaw, -sin pitch, cos -yaw) in Saturn axes (x, -y, -z). */
    double forward[3]={sin(-yaw)*cos(pitch),sin(pitch),-cos(-yaw)*cos(pitch)},right[3],up[3];
    const double world_up[3]={0,1,0},eye[3]={ex*96,height*96,-ez*96};
    static t3_stage_backdrop_line_t lines[224];
    static int32_t depth[224];
    int32_t m[16]={0},e[3];
    double worst_column=0,worst_row=0,worst_increment=0,worst_depth=0;
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
    for(int i=0;i<3;++i)right[i]=m[i]/ONE/fx,up[i]=m[4+i]/ONE/fy,forward[i]=m[12+i]/ONE;
    {
        double eye_q[3];
        for(int i=0;i<3;++i)eye_q[i]=e[i];
        assert(t3_stage_backdrop_lines(&B,m,e,turn,step,lines,depth));
        for(unsigned n=0;n<B.height/step;++n) {
            const double y=n*step+(step-1)/2.0;
            const ref_t r=reference(right,up,forward,fx,fy,eye_q,turn,y);
            double column,row,dc;
            if(!r.seen || r.row<(step-1)/2.0 || r.row>B.rows-1-(step-1)/2.0)continue;  /* clamped */
            column=fmod(lines[n].x/ONE+B.width/2*(lines[n].zoom/ONE),B.columns);
            row=lines[n].y/ONE+(step-1)/2.0;   /* the entry is the group's first line */
            dc=fabs(fmod(column-r.column+1.5*B.columns,B.columns)-0.5*B.columns);
            if(dc>worst_column)worst_column=dc;
            if(fabs(row-r.row)>worst_row)worst_row=fabs(row-r.row);
            if(fabs(lines[n].zoom/ONE-r.increment)>worst_increment)
                worst_increment=fabs(lines[n].zoom/ONE-r.increment);
            if(fabs(depth[n]/(r.depth*96)-1)>worst_depth)worst_depth=fabs(depth[n]/(r.depth*96)-1);
        }
    }
    /* A texel is about 2.7 dots across and 2 down: within a tenth of a dot
     * across. The row follows the elevation tangent, a curve, and is
     * interpolated linearly over 32 lines: within a third of a dot (the line
     * scroll itself misses by several texels, check_backdrop_mapping.py).
     * The increment is rounded to the VDP2's 1/256. */
    if(worst_column>0.04 || worst_row>0.15 || worst_increment>0.003 || worst_depth>0.002) {
        fprintf(stderr,"yaw %g pitch %g eye (%g %g %g) turn %d step %u: column %g row %g increment %g depth %g\n",
                yaw_deg,pitch_deg,ex,height,ez,turn,step,worst_column,worst_row,worst_increment,worst_depth);
        assert(0);
    }
}

int main(void) {
    static t3_stage_backdrop_line_t lines[224];
    int32_t m[16]={0},e[3]={0};
    /* Straight ahead along PS1 +z the centre column is 0 (the bake's yaw 0). */
    m[0]=178000;m[5]=238000;m[14]=-65536;
    assert(t3_stage_backdrop_lines(&B,m,e,0,1,lines,NULL));
    {
        const double column=fmod(lines[111].x/ONE+160*(lines[111].zoom/ONE)+B.columns/2,B.columns)-B.columns/2;
        assert(fabs(column)<0.05);
    }
    for(int yaw=-180;yaw<180;yaw+=35)
        for(int pitch=-5;pitch<=15;pitch+=10)
            check(yaw,pitch,(yaw%7)*120.0,700+(pitch+5)*70.0,(yaw%5)*-150.0,(yaw*13)&4095,1);
    check(20,8,300,1100,-200,512,2);
    check(-70,14,-900,2190,600,3900,4);
    check(160,0,0,737,0,0,8);
    /* A rolled camera and invalid steps are refused. */
    m[1]=0x2000;
    assert(!t3_stage_backdrop_lines(&B,m,e,0,1,lines,NULL));
    m[1]=0;
    assert(!t3_stage_backdrop_lines(&B,m,e,0,3,lines,NULL));
    return 0;
}
