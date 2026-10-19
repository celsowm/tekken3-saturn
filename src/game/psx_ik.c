#include "sim_profile.h"
#include "psx_ik.h"
#include <saturn/int_math.h>
#include <stdint.h>

#define ONE 4096
#define SQRT_FIRST 0x40u

typedef struct {t3_psx_matrix_t rot; t3_vec3i_t t;} frame_t;
static const unsigned k_bases[3]={2,3,7};
static const unsigned k_upper[4]={4,8,12,15};
static const unsigned k_lower[4]={5,9,13,16};
static const unsigned k_swivel[4]={15,16,17,18};
static const unsigned k_target[4]={3,6,9,12};

static int16_t signed16(uint16_t u) {
    return (int16_t)(u<0x8000u?(int32_t)u:(int32_t)u-65536);
}
static int32_t signed32(uint32_t u) {
    return u<0x80000000u?(int32_t)u:(int32_t)((int64_t)u-4294967296LL);
}
static int32_t w32(int64_t v) {return signed32((uint32_t)(uint64_t)v);}
static int32_t asr(int64_t v,unsigned bits) {
    return sat_floor_shift_i64(v,bits);
}
static int32_t trunc12(int32_t v) {return v<0?asr((int64_t)v+4095,12u):asr(v,12u);}
static int16_t sat16(int64_t v) {
    return (int16_t)(v>32767?32767:(v< -32768?-32768:v));
}
static int32_t read32(const uint8_t* p) {
    return signed32((uint32_t)p[0]|((uint32_t)p[1]<<8u)|
           ((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u));
}
static int16_t read16(const uint8_t* p) {
    return signed16((uint16_t)(p[0]|((uint16_t)p[1]<<8u)));
}
static int16_t trig_lookup(const t3_psx_trig_t* t,int16_t angle,int is_cos) {
    size_t p=(((uint16_t)angle>>3u)&0x1FFEu)+(is_cos?0x800u:0u);
    return read16(t->data+p);
}
static int table_ok(const t3_psx_ik_tables_t* table) {
    return table && table->trig.data &&
        table->trig.size==T3_PSX_TRIG_BYTES &&
        table->ik_blob && table->ik_size==T3_PSX_IK_BYTES;
}
static void vec3(const uint8_t* p,t3_vec3i_t* out) {
    out->x=read32(p);out->y=read32(p+4u);out->z=read32(p+8u);
}
static int safe_vec(t3_vec3i_t v) {
    return v.x>=-T3_PSX_UNIT_LIMIT && v.x<=T3_PSX_UNIT_LIMIT &&
           v.y>=-T3_PSX_UNIT_LIMIT && v.y<=T3_PSX_UNIT_LIMIT &&
           v.z>=-T3_PSX_UNIT_LIMIT && v.z<=T3_PSX_UNIT_LIMIT;
}
static void mvmva(const t3_psx_matrix_t* m,const int32_t v[3],int16_t out[3]) {
    unsigned i;
    for(i=0;i<3u;++i) {
        int64_t dot=(int64_t)m->m[i][0]*v[0]+
                    (int64_t)m->m[i][1]*v[1]+
                    (int64_t)m->m[i][2]*v[2];
        out[i]=sat16(asr(dot,12u));
    }
}
static int compose(frame_t* out,const frame_t* p,
                   const t3_psx_matrix_t* local,t3_vec3i_t off) {
    int32_t v[3]={off.x,off.y,off.z};
    int16_t d[3];
    int64_t x,y,z;
    if(!safe_vec(p->t)||!safe_vec(off))return 0;
    mvmva(&p->rot,v,d);
    x=(int64_t)p->t.x+d[0]; y=(int64_t)p->t.y+d[1];
    z=(int64_t)p->t.z+d[2];
    if(x< -T3_PSX_UNIT_LIMIT || x>T3_PSX_UNIT_LIMIT ||
       y< -T3_PSX_UNIT_LIMIT || y>T3_PSX_UNIT_LIMIT ||
       z< -T3_PSX_UNIT_LIMIT || z>T3_PSX_UNIT_LIMIT)return 0;
    out->t.x=(int32_t)x;out->t.y=(int32_t)y;out->t.z=(int32_t)z;
    return t3_psx_mul_rot(&p->rot,local,&out->rot);
}
static void transpose(const t3_psx_matrix_t* src,t3_psx_matrix_t* dst) {
    unsigned i,j;
    for(i=0;i<3u;++i)
        for(j=0;j<3u;++j)dst->m[i][j]=src->m[j][i];
}
static void split15(int32_t v,int32_t* hi,int32_t* lo) {
    if(v<0) {
        int64_t mag=-(int64_t)v;
        *hi=-(int32_t)(mag>>15u);
        *lo=-(int32_t)(mag&0x7FFF);
    } else {
        *hi=v>>15u;*lo=v&0x7FFF;
    }
}
/* Original IkLimbTarget splits 15-bit hi/low to avoid GTE precision loss. */
static int limb_target(const int16_t pose[49],unsigned start,
                       const frame_t* root,t3_vec3i_t off,int32_t out[3]) {
    t3_psx_matrix_t rot;
    int32_t hi[3],lo[3],a[3];
    int16_t high[3],low[3];
    unsigned i,j;
    const int32_t pos[3]={pose[start],pose[start+1u],pose[start+2u]};
    const int32_t base[3]={root->t.x,root->t.y,root->t.z};
    transpose(&root->rot,&rot);
    for(i=0;i<3u;++i)split15(pos[i]-base[i],&hi[i],&lo[i]);
    for(i=0;i<3u;++i) {
        int64_t dot=0;
        for(j=0;j<3u;++j)dot+=(int64_t)rot.m[i][j]*hi[j];
        high[i]=sat16(dot);
    }
    mvmva(&rot,lo,low);
    a[0]=low[0];a[1]=low[1];a[2]=low[2];
    out[0]=(int32_t)((int64_t)a[0]+(int64_t)high[0]*8+off.x);
    out[1]=(int32_t)((int64_t)a[1]+(int64_t)high[1]*8+off.y);
    out[2]=(int32_t)((int64_t)a[2]+(int64_t)high[2]*8+off.z);
    return 1;
}
static int square_root0(uint32_t a,const t3_psx_ik_tables_t* table,int32_t* out) {
    unsigned lz=0u,shift,x;
    uint32_t mant,value;
    if(a==0u){*out=0;return 1;}
    while((a & (0x80000000u>>lz))==0u && lz<31u)++lz;
    lz&=~1u;
    x=lz<24u?a>>(24u-lz):a<<(lz-24u);
    if(x<SQRT_FIRST || x>0xFFu)return 0;
    shift=(31u-lz)>>1u;
    mant=(uint32_t)(int32_t)read16(table->ik_blob+(x-SQRT_FIRST)*2u);
    value=(uint32_t)((((uint64_t)mant<<shift)&0xFFFFFFFFULL)>>12u);
    *out=(int32_t)value;
    return 1;
}
static void identity(t3_psx_matrix_t* m) {
    unsigned i,j;
    for(i=0;i<3u;++i)for(j=0;j<3u;++j)m->m[i][j]=(i==j)?ONE:0;
}
static void bend(t3_psx_matrix_t* m,int32_t sine,int32_t cosine) {
    int32_t a[3]={m->m[0][0],m->m[1][0],m->m[2][0]};
    unsigned i;
    for(i=0;i<3u;++i) {
        int32_t b=m->m[i][1];
        m->m[i][0]=signed16((uint16_t)asr((int64_t)a[i]*cosine+
                                                 (int64_t)b*sine,12u));
        m->m[i][1]=signed16((uint16_t)asr(-(int64_t)a[i]*sine+
                                                 (int64_t)b*cosine,12u));
    }
}
/* Return 1=solved, 0=too short / singular, -1=invalid coefficient table.
 * Original integer intermediate wraps are intentional, see research/pose.py. */
static int solve(const int32_t target[3],int16_t swivel,int arm,
                 const t3_psx_ik_tables_t* table,
                 t3_psx_matrix_t* upper,t3_psx_matrix_t* lower) {
    int32_t x=target[0],y=target[1],z=target[2];
    int32_t sq=w32((int64_t)x*x+(int64_t)y*y+(int64_t)z*z);
    int32_t length,inv,ux,uy,uz,k,a,e,b,p,q,r,w,v,cos_a,cos_b,sin_a,sin_b,off,diag;
    int32_t cs,ss,d;
    if(sq<=0)return 0;
    if(!square_root0((uint32_t)sq,table,&length))return -1;
    if(length<=0)return 0;
    inv=0x400000/length;
    ux=asr(w32((int64_t)x*inv),10u);
    uy=asr(w32((int64_t)y*inv),10u);
    uz=asr(w32((int64_t)z*inv),10u);
    k=ux+ONE;
    if(k==0) {
        k=-0x800;
        a=asr(w32((int64_t)y*inv),11u);
        e=-ONE;
    } else {
        if(k<0x2000) {
            k=-0x1000000/k;
            a=asr(w32((int64_t)k*uy),12u);
        } else {
            k=-0x800;
            a=asr(-uy,1u);
        }
        e=ONE;
    }
    b=asr(w32((int64_t)a*uy),12u)+e;
    k=asr(w32((int64_t)k*uz),12u);
    upper->m[0][0]=signed16((uint16_t)ux);
    upper->m[1][0]=signed16((uint16_t)uy);
    upper->m[2][0]=signed16((uint16_t)uz);
    cs=trig_lookup(&table->trig,swivel,1);
    ss=trig_lookup(&table->trig,swivel,0);
    p=asr(w32((int64_t)a*(e+ux)),12u);
    q=asr(w32((int64_t)k*(e+ux)),12u);
    r=asr(w32((int64_t)k*uy),12u);
    w=asr(w32((int64_t)a*uz),12u);
    v=asr(w32((int64_t)k*uz),12u)+ONE;
    upper->m[0][1]=signed16((uint16_t)asr(w32((int64_t)cs*p+(int64_t)ss*q),12u));
    upper->m[1][1]=signed16((uint16_t)asr(w32((int64_t)cs*b+(int64_t)ss*r),12u));
    upper->m[2][1]=signed16((uint16_t)asr(w32((int64_t)cs*w+(int64_t)ss*v),12u));
    upper->m[0][2]=signed16((uint16_t)asr(w32(-(int64_t)ss*p+(int64_t)cs*q),12u));
    upper->m[1][2]=signed16((uint16_t)asr(w32(-(int64_t)ss*b+(int64_t)cs*r),12u));
    upper->m[2][2]=signed16((uint16_t)asr(w32(-(int64_t)ss*w+(int64_t)cs*v),12u));
    if(arm) {
        if(length>0x215){identity(lower);return 1;}
        if(length<0x47)return 0;
        d=asr(w32((int64_t)inv*0x9204),11u);
        cos_a=asr(w32(((int64_t)length*0x800+d)*0xD9),16u);
        cos_b=asr(w32(((int64_t)length*0x800-d)*0x11A),16u);
    } else {
        if(length>0x37B){identity(lower);return 1;}
        if(length<3)return 0;
        d=asr(w32((int64_t)inv*0x6F8),11u);
        cos_a=asr(w32(((int64_t)length*0x800+d)*0x92),16u);
        cos_b=asr(w32(((int64_t)length*0x800-d)*0x93),16u);
    }
    if(cos_a>ONE){cos_a=ONE;sin_a=0;}
    else if(cos_a< -ONE){cos_a=-ONE;sin_a=0;}
    else {
        if(!square_root0((uint32_t)(0x1000000-cos_a*cos_a),table,&sin_a))
            return -1;
        if(!arm)sin_a=-sin_a;
    }
    bend(upper,signed16((uint16_t)sin_a),cos_a);
    if(cos_b>ONE){cos_b=ONE;sin_b=0;}
    else if(cos_b< -ONE){cos_b=-ONE;sin_b=0;}
    else sin_b=asr(w32((int64_t)sin_a*(arm?-0x14D4:-0x1012)),12u);
    diag=signed16((uint16_t)trunc12(w32((int64_t)cos_a*cos_b+
                                        (int64_t)sin_a*sin_b)));
    off=signed16((uint16_t)trunc12(w32(-(int64_t)sin_a*cos_b+
                                       (int64_t)cos_a*sin_b)));
    lower->m[0][0]=signed16((uint16_t)diag);
    lower->m[0][1]=signed16((uint16_t)-off);
    lower->m[0][2]=0;
    lower->m[1][0]=signed16((uint16_t)off);
    lower->m[1][1]=signed16((uint16_t)diag);
    lower->m[1][2]=0;
    lower->m[2][0]=0;
    lower->m[2][1]=0;
    lower->m[2][2]=ONE;
    return 1;
}
int T3_SIM_IMPL(t3_psx_pose_ik)(const int16_t pose[T3_PSX_POSE_CHANNELS],
                   const t3_psx_ik_tables_t* table,
                   const t3_psx_matrix_t input[T3_PSX_ROTATION_SLOTS],
                   uint32_t input_mask,
                   t3_psx_matrix_t output[T3_PSX_ROTATION_SLOTS],
                   uint32_t* output_mask) {
    t3_psx_matrix_t result[T3_PSX_ROTATION_SLOTS];
    t3_psx_matrix_t basis;
    t3_vec3i_t skeletal[4],offsets[4];
    frame_t root,chest,right,left,hips,fr[4];
    int32_t targets[4][3];
    uint32_t mask;
    unsigned i,j,k;
    if(!pose || !input || !output || !output_mask)return -1;
    if(!table_ok(table))return 0;
    if((input_mask & 0x24CCFu)!=0x24CCFu)return -1; /* all 10 FK slots */
    for(i=0;i<T3_PSX_ROTATION_SLOTS;++i)result[i]=input[i];
    for(i=0;i<3u;++i)
        for(j=0;j<3u;++j)
            basis.m[i][j]=read16(table->ik_blob+T3_PSX_SQRT_BYTES+
                                              (i*3u+j)*2u);
    for(i=0;i<4u;++i) {
        vec3(table->ik_blob+T3_PSX_SQRT_BYTES+18u+i*12u,&skeletal[i]);
        vec3(table->ik_blob+T3_PSX_SQRT_BYTES+18u+48u+i*12u,&offsets[i]);
        if(!safe_vec(skeletal[i]) || !safe_vec(offsets[i]))return -1;
    }
    for(i=0;i<3u;++i) {
        unsigned slot=k_bases[i];
        t3_psx_matrix_t m;
        if(!t3_psx_mul_rot(&result[slot],&basis,&m))return -1;
        result[slot]=m;
    }
    root.rot=result[0];
    root.t.x=pose[0];root.t.y=pose[1];root.t.z=pose[2];
    if(!compose(&chest,&root,&result[1],skeletal[0]) ||
       !compose(&right,&chest,&result[3],skeletal[1]) ||
       !compose(&left,&chest,&result[7],skeletal[2]) ||
       !compose(&hips,&root,&result[11],skeletal[3]))
        return -1;
    fr[0]=right;fr[1]=left;fr[2]=hips;fr[3]=hips;
    mask=input_mask;
    for(i=0;i<4u;++i) {
        t3_psx_matrix_t upper,lower;
        int rc;
        if(!limb_target(pose,k_target[i],&fr[i],offsets[i],targets[i]))
            return -1;
        rc=solve(targets[i],pose[k_swivel[i]],i<2u,table,&upper,&lower);
        if(rc<0)return -1;
        if(rc==0)continue; /* singular limbs are unknown, not identity */
        result[k_upper[i]]=upper;result[k_lower[i]]=lower;
        mask|=((uint32_t)1u<<k_upper[i])|((uint32_t)1u<<k_lower[i]);
    }
    for(k=0;k<T3_PSX_ROTATION_SLOTS;++k)output[k]=result[k];
    *output_mask=mask;
    return 1;
}
T3_SIM_WRAP(int,t3_psx_pose_ik,T3_SIM_IK,
    (const int16_t pose[T3_PSX_POSE_CHANNELS],
                   const t3_psx_ik_tables_t* table,
                   const t3_psx_matrix_t input[T3_PSX_ROTATION_SLOTS],
                   uint32_t input_mask,
                   t3_psx_matrix_t output[T3_PSX_ROTATION_SLOTS],
                   uint32_t* output_mask),
    (pose,table,input,input_mask,output,output_mask))

int t3_psx_pose_full(const int16_t pose[T3_PSX_POSE_CHANNELS],
                     const t3_psx_ik_tables_t* table,
                     t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],
                     uint32_t* valid_slots) {
    t3_psx_matrix_t local[T3_PSX_ROTATION_SLOTS];
    uint32_t mask=0u;
    int rc;
    if(!pose || !slots || !valid_slots)return -1;
    if(!table_ok(table))return 0;
    if(!t3_psx_pose_fk(pose,&table->trig,local,&mask))return -1;
    rc=t3_psx_pose_ik(pose,table,local,mask,slots,valid_slots);
    return rc;
}
