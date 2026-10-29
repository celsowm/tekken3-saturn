/* Line-by-line mapping of the baked stage panorama; see stage_backdrop.h.
 *
 * The panorama's placement (scaled by 8 around the fighters' midpoint, the
 * per-stage height, the Ry(-turn) turn) follows Tekken3Remake's
 * docs/research/code/stages.md and remake/presentation/stage/stage_view.gd
 * (PolyForm Noncommercial 1.0.0, Copyright (c) 2026 n0madic; see NOTICE.md).
 * The cylinder mapping is this project's own. Integer only: the Saturn has
 * no FPU. Values are 32-bit, products go through the SH-2's 32x32->64
 * multiplier, and every division through its 64/32 divider. */
#include "stage_backdrop.h"

#define ONE 65536
/* |right.y| above this (16.16) is a rolled camera, as in the VDP2 floor. */
#define ROLL_LIMIT 0x120
/* Rays per fitted line, evenly across the screen: the line's scroll and
 * increment are their least-squares fit, its row their mean. */
#define SAMPLES 5
/* Screen lines between fitted lines; the lines between are interpolated.
 * Every 8th line instead changes the fit by under 0.1 texel (see
 * docs/PAUL_STAGE_VDP2.md), every 56th starts to bend steep views. */
#define KEY 32
#define MAX_HEIGHT 512
#define MAX_BOX_HALF 16000
/* Elevation tangents beyond this (16.16) are off the picture anyway. */
#define TAN_LIMIT (16*ONE)

/* Signed 64/32 division whose quotient fits in 32 bits: on the SH-2 the
 * on-chip divider (39 cycles) instead of libgcc's bit-serial __divdi3. */
static int32_t divide(int64_t n,int32_t d) {
#if defined(__sh__)
    *(volatile uint32_t*)0xFFFFFF00u=(uint32_t)d;
    *(volatile uint32_t*)0xFFFFFF10u=(uint32_t)((uint64_t)n>>32);
    *(volatile uint32_t*)0xFFFFFF14u=(uint32_t)n;
    return (int32_t)*(volatile uint32_t*)0xFFFFFF14u;
#else
    return (int32_t)(n/d);
#endif
}

/* a * b >> 16 and a * b >> 30 (floor). */
static inline int32_t mul16(int32_t a,int32_t b) { return (int32_t)(((int64_t)a*b)>>16); }
static inline int32_t mul30(int32_t a,int32_t b) { return (int32_t)(((int64_t)a*b)>>30); }

static uint64_t isqrt64(uint64_t v) {
    uint64_t r=0,bit=UINT64_C(1)<<62;
    while(bit>v)bit>>=2;
    while(bit) {
        if(v>=r+bit){v-=r+bit;r=(r>>1)+bit;}
        else r>>=1;
        bit>>=2;
    }
    return r;
}


static int64_t length3(const int32_t* v) {
    return (int64_t)isqrt64((uint64_t)((int64_t)v[0]*v[0]+(int64_t)v[1]*v[1]+(int64_t)v[2]*v[2]));
}

/* atan2(y, x) as a fraction of a turn in 16.16 (0..ONE), and the length of
 * (x, y) in `length`, for |x|, |y| < 2^30. With z = min / max, Abramowitz
 * and Stegun 4.4.49 for atan z (within 1e-5 radian) and max sqrt(1 + z^2)
 * (a degree 5 fit, within 1.5e-6), so no square root is taken. */
static int32_t atan2_turn(int32_t y,int32_t x,int32_t* length) {
    static const int32_t a[5]={1073597943,-354656388,193424926,-91410863,22371518};
    static const int32_t h[6]={1073743374,536752778,-132721058,59845195,-24454633,5335775};
    const int32_t ax=x<0?-x:x,ay=y<0?-y:y,big=ay>ax?ay:ax;
    int32_t z,z2,p,r;
    *length=0;
    if(!big)return 0;
    z=divide((int64_t)(ay>ax?ax:ay)<<30,big);
    z2=mul30(z,z);
    p=h[5];
    for(int i=4;i>=0;--i)p=h[i]+mul30(p,z2);
    *length=mul30(big,p);
    p=a[4];
    for(int i=3;i>=0;--i)p=a[i]+mul30(p,z2);
    r=mul30(p,z);                              /* radians, 2.30 */
    r=(int32_t)(((int64_t)r*41722)>>32);       /* turns, 16.16 */
    if(ay>ax)r=ONE/4-r;
    if(x<0)r=ONE/2-r;
    if(y<0)r=-r;
    return r&(ONE-1);
}

/* sin of `a` (4096 a turn) in 2.30. Twice a call: 64 bits, as x^2 reaches
 * 2.5. */
static int32_t sin4096(int32_t a) {
    int64_t x,x2,p;
    int neg=0;
    a&=4095;
    if(a>=2048){a-=2048;neg=1;}
    if(a>1024)a=2048-a;
    x=(int64_t)a*1647099;                      /* radians, 2.30 */
    x2=(x*x)>>30;
    p=2959;                                    /* x - x^3/3! + ... + x^9/9! */
    p=213044-((p*x2)>>30);
    p=8947849-((p*x2)>>30);
    p=178956971-((p*x2)>>30);
    p=(INT64_C(1)<<30)-((p*x2)>>30);
    p=(p*x)>>30;
    return (int32_t)(neg?-p:p);
}

/* The wall hit along a ray in the panorama's frame (PS1 axes, y down):
 * `e` the eye (8 fractional bits), `d` the direction (16.16, its forward
 * component 1). Returns the depth along the forward axis (8 fractional bits)
 * and the hit point, or 0 when the ray is too steep to reach a wall. */
static int32_t hit(int32_t wall,const int32_t e[3],const int32_t d[3],int32_t p[3]) {
    int32_t t=-1;
    for(int axis=0;axis<3;axis+=2) {
        const uint32_t ad=d[axis]<0?-(uint32_t)d[axis]:(uint32_t)d[axis];
        const int64_t n=(int64_t)((d[axis]>0?wall:-wall)-e[axis])*ONE;
        int32_t s;
        if(ad<64)continue;                     /* within 1/1000 of vertical */
        if((uint64_t)(n<0?-n:n)>>31>=ad)continue;   /* beyond any other wall */
        s=divide(n,d[axis]);
        if(s>0 && (t<0 || s<t))t=s;
    }
    if(t<=0)return 0;
    p[0]=e[0]+mul16(t,d[0]);p[2]=e[2]+mul16(t,d[2]);
    {
        /* Steep rays reach far above or below the picture: keep the height
         * within 32 bits, the row clamps anyway. */
        int64_t y=e[1]+(((int64_t)t*d[1])>>16);
        if(y>INT32_MAX/2)y=INT32_MAX/2;
        if(y< -INT32_MAX/2)y=-INT32_MAX/2;
        p[1]=(int32_t)y;
    }
    return t;
}

/* The picture position (16.16 column and row) the bake point sees at wall
 * point `p`. */
static void picture(const t3_stage_backdrop_t* b,const int32_t p[3],int32_t* column,int32_t* row) {
    /* The horizontal distance is at least the wall distance. */
    int32_t rho;
    const int32_t angle=atan2_turn(p[0],p[2],&rho);
    const int32_t up=-p[1]-(b->bake_height<<8);
    int32_t tan;
    if(up>=(int64_t)rho*(TAN_LIMIT/ONE))tan=TAN_LIMIT;
    else if(up<=-(int64_t)rho*(TAN_LIMIT/ONE))tan=-TAN_LIMIT;
    else tan=rho?divide((int64_t)up*ONE,rho):0;
    *column=angle*b->columns;
    *row=mul16(b->tan_top-tan,b->rows_per_tan);
}

/* Wraps `step` into [-wrap/2, wrap/2). */
static int32_t nearest(int32_t step,int32_t wrap) {
    while(step>=wrap/2)step-=wrap;
    while(step< -wrap/2)step+=wrap;
    return step;
}

typedef struct { int32_t column,zoom,row,depth; int ok; } fit_t;

typedef struct {
    const t3_stage_backdrop_t* b;
    int32_t e[3],right[3],up[3],forward[3],across[SAMPLES];
    int32_t fy,fl,c,s,wall,wrap,spread;
} view_t;

/* The least-squares fit of screen line `y2` (half dots). */
static fit_t fit(const view_t* w,int32_t y2) {
    const t3_stage_backdrop_t* b=w->b;
    const int32_t v=divide((int64_t)(b->height-y2)*ONE,b->height);
    const int32_t a=divide((int64_t)v*w->fl,w->fy);         /* v / focal_y, 16.16 */
    int32_t col[SAMPLES],t=0,sum=0;
    int64_t rows=0,dc=0;
    fit_t f={0};
    for(int k=0;k<SAMPLES;++k) {
        int32_t r[3],d[3],p[3],row,depth;
        for(int i=0;i<3;++i)
            r[i]=w->forward[i]+(int32_t)(((int64_t)a*w->up[i]+(int64_t)w->across[k]*w->right[i])>>16);
        d[0]=(int32_t)(((int64_t)r[0]*w->c-(int64_t)r[2]*w->s)>>16);d[1]=-r[1];
        d[2]=(int32_t)((-(int64_t)r[0]*w->s-(int64_t)r[2]*w->c)>>16);
        if(!(depth=hit(w->wall,w->e,d,p)))return f;
        picture(b,p,&col[k],&row);
        /* Unwrap against the previous sample. */
        if(k)col[k]=col[k-1]+nearest(col[k]-col[k-1],w->wrap);
        rows+=row;
        if(!t || depth<t)t=depth;
    }
    /* Centred on the screen's middle, where the sample offsets sum to 0. */
    for(int k=0;k<SAMPLES;++k) {
        sum+=col[k]-col[0];
        dc+=(int64_t)((2*k+1-SAMPLES)*b->width)*(col[k]-col[0]);
    }
    f.column=col[0]+divide(sum,SAMPLES);
    f.zoom=divide(dc*2*SAMPLES,w->spread);
    f.row=divide(rows,SAMPLES);
    f.depth=(int32_t)(((int64_t)t*3)>>3);     /* PS1 8.8 to Saturn 16.16: 96/256 */
    f.ok=1;
    return f;
}

static int32_t lerp(int32_t a,int32_t b,int32_t frac) {
    return a+mul16(b-a,frac);
}

int t3_stage_backdrop_lines_offset(const t3_stage_backdrop_t* b,const int32_t m[16],const int32_t eye[3],
                            int32_t turn,unsigned step,int vertical_offset,t3_stage_backdrop_line_t* lines,int32_t* wall_depth) {
#if defined(T3_STAGE_STACK_PROBE)
    extern void t3_stage_stack_sample(unsigned);
    t3_stage_stack_sample(1);
#endif
    view_t w;
    fit_t keys[MAX_HEIGHT/KEY+2];
    unsigned count,key_count,first_ok=0;
    int64_t fx,fy,fl;
    int32_t top,bottom;
    /* Columns are a power of two: the layer wraps at its bitmap width. */
    if(!b || !m || !eye || !lines || !b->columns || b->columns>1024 || (b->columns&(b->columns-1)) ||
       b->rows<9 || b->width<2 || b->width>1024 || b->height<2 || b->height>MAX_HEIGHT ||
       b->box_half<=0 || b->box_half>MAX_BOX_HALF || b->rows_per_tan<=0 ||
       b->rows_per_tan>(INT32_MAX/(2*TAN_LIMIT/ONE)) || vertical_offset< -MAX_HEIGHT || vertical_offset>MAX_HEIGHT ||
       (step!=1 && step!=2 && step!=4 && step!=8))return 0;
    w.b=b;
    w.wrap=(int32_t)b->columns*ONE;
    w.wall=b->box_half<<8;
    fx=length3(m);fy=length3(m+4);fl=length3(m+12);
    if(!fx || !fy || !fl || fx>INT32_MAX || fy>INT32_MAX || fl>INT32_MAX)return 0;
    w.fy=(int32_t)fy;w.fl=(int32_t)fl;
    for(int i=0;i<3;++i) {
        w.right[i]=divide((int64_t)m[i]*ONE,(int32_t)fx);w.up[i]=divide((int64_t)m[4+i]*ONE,(int32_t)fy);
        w.forward[i]=divide((int64_t)m[12+i]*ONE,(int32_t)fl);
    }
    if(w.right[1]>ROLL_LIMIT || w.right[1]< -ROLL_LIMIT)return 0;
    /* Dot (2k+1) * width / (2 SAMPLES), as NDC over focal_x. */
    w.spread=0;
    for(int k=0;k<SAMPLES;++k) {
        w.across[k]=divide((int64_t)((2*k+1-SAMPLES)*ONE/SAMPLES)*fl,(int32_t)fx);
        w.spread+=(2*k+1-SAMPLES)*(2*k+1-SAMPLES);
    }
    w.spread*=b->width*b->width;               /* the offsets' sum of squares, (2 SAMPLES)^2 dots^2 */
    /* Saturn x, y, z are PS1 x, -y, -z, 96 fx16 a PS1 unit (8/3 in 8
     * fractional bits); the panorama turned by Ry(-turn) puts a world
     * point w at local (w.x c + w.z s, w.y, -w.x s + w.z c). */
    w.c=sin4096(turn+1024)>>14;w.s=sin4096(turn)>>14;          /* 16.16 */
    {
        const int32_t x=divide((int64_t)eye[0]*8,3),z=-divide((int64_t)eye[2]*8,3);
        w.e[0]=mul16(x,w.c)+mul16(z,w.s);w.e[1]=-divide((int64_t)eye[1]*8,3);
        w.e[2]=mul16(z,w.c)-mul16(x,w.s);
    }
    /* Fitted lines at 0, KEY, 2 KEY, ... up to the last screen line. */
    key_count=(b->height-1u+KEY-1u)/KEY+1u;
    for(unsigned j=0;j<key_count;++j) {
        keys[j]=fit(&w,(int32_t)(2*KEY*j)+2*vertical_offset);
        if(keys[j].ok && !keys[first_ok].ok)first_ok=j;
        if(j && keys[j].ok) {
            /* Unwrap against the previous fitted line. */
            const fit_t* before=keys[j-1].ok?&keys[j-1]:&keys[first_ok];
            keys[j].column=before->column+nearest(keys[j].column-before->column,w.wrap);
        }
    }
    if(!keys[first_ok].ok) {
        for(unsigned n=0;n<b->height/step;++n) {
            lines[n]=(t3_stage_backdrop_line_t){0,0,ONE};
            if(wall_depth)wall_depth[n]=0;
        }
        return 1;
    }
    /* A line with no wall (straight up or down) takes its neighbour's picture
     * and does not cut the floor. */
    for(unsigned j=0;j<key_count;++j)
        if(!keys[j].ok) {
            const fit_t* near=j?&keys[j-1]:&keys[first_ok];
            keys[j]=(fit_t){near->column,near->zoom,near->row,0,0};
        }
    count=b->height/step;
    top=(int32_t)(step-1)*ONE/2;bottom=((int32_t)b->rows-1)*ONE-top;
    /* One segment between fitted lines j and j + 1 at a time. */
    for(unsigned n=0;n<count;) {
        /* Screen line y (in half dots) at the group's centre. */
        const unsigned y2=2*n*step+step-1;
        const unsigned j=y2/(2*KEY);
        /* The first group past the segment, whose y2 reaches 2 KEY (j + 1). */
        const unsigned end=(2*KEY*(j+1)+step)/(2*step)<count?(2*KEY*(j+1)+step)/(2*step):count;
        const unsigned first=n;
        const fit_t* k0=&keys[j];
        const fit_t* k1=&keys[j+1<key_count?j+1:j];
        const int32_t half=(int32_t)(b->width/2),mask=w.wrap-1,frac_step=(int32_t)(2*step)*(ONE/(2*KEY));
        const int32_t frac=(int32_t)(y2-2*KEY*j)*(ONE/(2*KEY));
        /* Stepped a group at a time: the dropped remainders add up to under
         * a thousandth of a texel over a segment. */
        const int32_t zoom_step=mul16(k1->zoom-k0->zoom,frac_step),row_step=mul16(k1->row-k0->row,frac_step);
        const int32_t column_step=mul16(k1->column-k0->column,frac_step);
        int32_t zoom_at=lerp(k0->zoom,k1->zoom,frac),row_at=lerp(k0->row,k1->row,frac);
        int32_t column_at=lerp(k0->column,k1->column,frac);
        for(;n<end;++n,zoom_at+=zoom_step,row_at+=row_step,column_at+=column_step) {
            /* The VDP2 keeps 8 fractional bits of the increment; the scroll
             * is taken after rounding so the screen's middle stays exact. */
            int32_t zoom=(zoom_at+128)&~255,row=row_at;
            if(zoom<256)zoom=256;
            if(row<top)row=top;
            if(row>bottom)row=bottom;
            lines[n].x=(column_at-half*zoom)&mask;
            /* Each entry replaces the layer's vertical coordinate, which then
             * advances a row a line: the group's first line gets its own row. */
            lines[n].y=row-top;
            lines[n].zoom=(uint32_t)zoom;
        }
        if(wall_depth) {
            const int depth=k0->ok && k1->ok;
            const int32_t depth_step=depth?mul16(k1->depth-k0->depth,frac_step):0;
            int32_t depth_at=depth?lerp(k0->depth,k1->depth,frac):0;
            for(unsigned i=first;i<end;++i,depth_at+=depth_step)wall_depth[i]=depth_at;
        }
    }
    return 1;
}
int t3_stage_backdrop_lines(const t3_stage_backdrop_t* b,const int32_t m[16],const int32_t eye[3],
                            int32_t turn,unsigned step,t3_stage_backdrop_line_t* lines,int32_t* wall_depth) {
    return t3_stage_backdrop_lines_offset(b,m,eye,turn,step,0,lines,wall_depth);
}
