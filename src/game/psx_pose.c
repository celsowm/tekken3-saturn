#include "sim_profile.h"
#include "psx_pose.h"
#include <saturn/int_math.h>
#include <stdint.h>

static const uint8_t k_slots[10]={0u,1u,11u,2u,3u,7u,6u,10u,14u,17u};

static int16_t sat16(int32_t v) {
    return (int16_t)(v>32767 ? 32767 : (v< -32768 ? -32768 : v));
}
static int16_t wrap16(int32_t v) {
    uint16_t w=(uint16_t)v;
    return (int16_t)(w<0x8000u?(int32_t)w:(int32_t)w-65536);
}
static int16_t gpf(int32_t a,int32_t b) {
    /* Both operands originate in signed 16-bit GTE IR registers. */
    return sat16(sat_floor_shift_i32(a*b,12u));
}
static int16_t sine(const t3_psx_trig_t* table,uint16_t angle,unsigned cos) {
    size_t index=(size_t)(((uint32_t)angle>>3u)&0x1FFEu)+
                 (cos?0x800u:0u);
    uint16_t w=(uint16_t)(table->data[index] |
        ((uint16_t)table->data[index+1u]<<8u));
    return wrap16((int32_t)w);
}
static int valid_table(const t3_psx_trig_t* table) {
    return table && table->data && table->size==T3_PSX_TRIG_BYTES;
}

int t3_psx_euler_matrix(int16_t x,int16_t y,int16_t z,
                  const t3_psx_trig_t* trig,t3_psx_matrix_t* out) {
    if(!valid_table(trig) || !out)return 0;
    /* Port of the original GTE sequence documented in
     * Tekken3Remake tools/research/pose.py EulerToMatrix.
     * GPF (IR) saturation happens at each intermediate multiply. */
    int16_t sx=sine(trig,(uint16_t)x,0u),cx=sine(trig,(uint16_t)x,1u);
    int16_t sy=sine(trig,(uint16_t)y,0u),cy=sine(trig,(uint16_t)y,1u);
    int16_t sz=sine(trig,(uint16_t)z,0u),cz=sine(trig,(uint16_t)z,1u);
    int16_t a1=gpf(sx,cz),a2=gpf(sx,sz),a3=gpf(sx,cy);
    int16_t b1=gpf(sy,a1),b2=gpf(sy,a2);
    int16_t cxcz=gpf(cx,cz);
    int16_t b3=gpf(sy,cxcz),c1=gpf(cy,cz),c2=gpf(cy,sz),c3=gpf(cy,cx);
    int16_t cxsz=gpf(cx,sz);
    out->m[2][1]=a3;
    out->m[2][0]=wrap16(-sy);
    out->m[0][1]=wrap16((int32_t)b1-cxsz);
    out->m[1][1]=wrap16((int32_t)cxcz+b2);
    out->m[0][2]=wrap16((int32_t)a2+b3);
    out->m[1][2]=wrap16(sat_floor_shift_i32((int32_t)cxsz*sy,12u)-a1);
    out->m[0][0]=c1;
    out->m[1][0]=c2;
    out->m[2][2]=c3;
    return 1;
}
int T3_SIM_IMPL(t3_psx_pose_fk)(const int16_t pose[T3_PSX_POSE_CHANNELS],
                   const t3_psx_trig_t* table,
                   t3_psx_matrix_t out[T3_PSX_ROTATION_SLOTS],
                   uint32_t* valid_slots) {
    t3_psx_matrix_t result[T3_PSX_ROTATION_SLOTS]={0};
    uint32_t mask=0u;
    unsigned i;
    if (!pose || !valid_table(table) || !out || !valid_slots) return 0;
    for(i=0;i<10u;++i) {
        unsigned ch=19u+3u*i;
        t3_psx_euler_matrix(pose[ch],pose[ch+1u],pose[ch+2u],table,&result[k_slots[i]]);
        mask|=(uint32_t)1u<<k_slots[i];
    }
    __builtin_memcpy(out,result,sizeof(result)); /* one call, not one per matrix */
    *valid_slots=mask;
    return 1;
}
T3_SIM_WRAP(int,t3_psx_pose_fk,T3_SIM_FK,
    (const int16_t pose[T3_PSX_POSE_CHANNELS],
                   const t3_psx_trig_t* table,
                   t3_psx_matrix_t out[T3_PSX_ROTATION_SLOTS],
                   uint32_t* valid_slots),
    (pose,table,out,valid_slots))

#define T3_BITS4(n) UINT32_C(1)<<(n),UINT32_C(1)<<((n)+1),UINT32_C(1)<<((n)+2),UINT32_C(1)<<((n)+3)
const uint32_t t3_psx_bits[32]={T3_BITS4(0),T3_BITS4(4),T3_BITS4(8),T3_BITS4(12),
    T3_BITS4(16),T3_BITS4(20),T3_BITS4(24),T3_BITS4(28)};
#undef T3_BITS4

static int small_entries(const t3_psx_matrix_t* m) {
    const int16_t* v=&m->m[0][0];
    uint32_t outside=0u;
    /* v+16384 leaves 0..32767 only for v in -16384..16383. */
    for(unsigned k=0;k<9u;++k)outside|=(uint32_t)(v[k]+16384)&~32767u;
    return outside==0u;
}
#if defined(__sh__)
/* sat16(floor((a . b) / 4096)) for halfword triples: MAC.W keeps the exact
 * 64-bit sum (S is clear), which the two C paths below also compute. */
static inline int16_t dot3_12(const int16_t* a,const int16_t* b) {
    int32_t hi,lo;
    __asm__("clrmac\n\tmac.w @%0+,@%1+\n\tmac.w @%0+,@%1+\n\tmac.w @%0+,@%1+\n\t"
            "sts mach,%2\n\tsts macl,%3"
            :"+r"(a),"+r"(b),"=r"(hi),"=r"(lo)
            :"m"(*(const int16_t(*)[3])a),"m"(*(const int16_t(*)[3])b)
            :"mach","macl");
    if(hi!=lo>>31)return hi<0?-32768:32767;
    return sat16(t3_psx_floor12(lo));
}
#endif
int t3_psx_mul_rot(const t3_psx_matrix_t* a,const t3_psx_matrix_t* b,
                   t3_psx_matrix_t* out) {
    t3_psx_matrix_t r={0};
    unsigned i,j;
    if(!a || !b || !out)return 0;
#if defined(__sh__)
    {
        const int16_t bt[3][3]={{b->m[0][0],b->m[1][0],b->m[2][0]},
            {b->m[0][1],b->m[1][1],b->m[2][1]},{b->m[0][2],b->m[1][2],b->m[2][2]}};
        for(i=0;i<3u;++i)
            for(j=0;j<3u;++j)r.m[i][j]=dot3_12(a->m[i],bt[j]);
        t3_psx_matrix_copy(out,&r);
        return 1;
    }
#endif
    if(small_entries(a) && small_entries(b)) {
        /* |entry| <= 16384: three products sum within 32 bits. */
        for(j=0;j<3u;++j)
            for(i=0;i<3u;++i)
                r.m[i][j]=sat16(t3_psx_floor12(a->m[i][0]*b->m[0][j]+
                    a->m[i][1]*b->m[1][j]+a->m[i][2]*b->m[2][j]));
        t3_psx_matrix_copy(out,&r);
        return 1;
    }
    /* Each 16x16 product fits 32 bits. floor(sum/4096) is the sum of each
     * product's floor plus the carry of their low 12 bits, so the SH-2 needs
     * no 64-bit arithmetic for the same result. */
    for(j=0;j<3u;++j)
        for(i=0;i<3u;++i) {
            const int32_t p0=(int32_t)a->m[i][0]*b->m[0][j];
            const int32_t p1=(int32_t)a->m[i][1]*b->m[1][j];
            const int32_t p2=(int32_t)a->m[i][2]*b->m[2][j];
            r.m[i][j]=sat16(sat_floor_shift_i32(p0,12u)+sat_floor_shift_i32(p1,12u)+sat_floor_shift_i32(p2,12u)+
                (int32_t)((uint32_t)((p0&4095)+(p1&4095)+(p2&4095))>>12));
        }
    t3_psx_matrix_copy(out,&r);
    return 1;
}
static int valid_vec(t3_vec3i_t p) {
    return p.x>=-T3_PSX_UNIT_LIMIT && p.x<=T3_PSX_UNIT_LIMIT &&
           p.y>=-T3_PSX_UNIT_LIMIT && p.y<=T3_PSX_UNIT_LIMIT &&
           p.z>=-T3_PSX_UNIT_LIMIT && p.z<=T3_PSX_UNIT_LIMIT;
}
static int place(const t3_psx_joint_t* parent,const t3_vec3i_t* delta,
                 t3_vec3i_t* out) {
    int32_t coords[3];
    const int32_t x[3]={delta->x,delta->y,delta->z}; /* within the unit limit */
    unsigned i;
    for(i=0;i<3u;++i) {
        coords[i]=(i==0u?parent->origin.x:(i==1u?parent->origin.y:parent->origin.z))+
            t3_psx_dot12(parent->rotation.m[i],x);
        if(coords[i]<-T3_PSX_UNIT_LIMIT || coords[i]>T3_PSX_UNIT_LIMIT)
            return 0;
    }
    out->x=coords[0];
    out->y=coords[1];
    out->z=coords[2];
    return 1;
}
int T3_SIM_IMPL(t3_psx_compose_parts)(const t3_psx_part_t parts[T3_PSX_DRAW_PARTS],
                         const t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],
                         uint32_t valid_slots,const t3_psx_joint_t* root,
                         t3_psx_joint_t out[T3_PSX_DRAW_PARTS],
                         uint32_t* valid_parts) {
    t3_psx_joint_t result[T3_PSX_DRAW_PARTS]={0};
    uint32_t mask=0u;
    unsigned i;
    if (!parts || !slots || !root || !out || !valid_parts ||
        !valid_vec(root->origin) || (valid_slots>>T3_PSX_ROTATION_SLOTS))
        return 0;
    for(i=0;i<T3_PSX_DRAW_PARTS;++i) {
        const t3_psx_part_t* p=&parts[i];
        const t3_psx_joint_t* par;
        if(p->parent < -1 || p->parent >= (int)i ||
           p->slot>=T3_PSX_DRAW_PARTS || !valid_vec(p->offset))
            return 0;
        /* Slots 18..21 are costume-attachment rotations: no IK/FK
         * equivalent until attachment dynamics are implemented. */
        if(p->slot>=T3_PSX_ROTATION_SLOTS ||
           !(valid_slots & t3_psx_bit(p->slot)))continue;
        if(p->parent>=0 && !(mask & t3_psx_bit((unsigned)p->parent)))continue;
        par=p->parent<0?root:&result[p->parent];
        if(!t3_psx_mul_rot(&par->rotation,&slots[p->slot],
                           &result[i].rotation) ||
           !place(par,&p->offset,&result[i].origin))return 0;
        mask|=t3_psx_bit(i);
    }
    __builtin_memcpy(out,result,sizeof(result)); /* one call, not one per part */
    *valid_parts=mask;
    return 1;
}
T3_SIM_WRAP(int,t3_psx_compose_parts,T3_SIM_PARTS,
    (const t3_psx_part_t parts[T3_PSX_DRAW_PARTS],
                         const t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],
                         uint32_t valid_slots,const t3_psx_joint_t* root,
                         t3_psx_joint_t out[T3_PSX_DRAW_PARTS],
                         uint32_t* valid_parts),
    (parts,slots,valid_slots,root,out,valid_parts))

int t3_psx_parts_to_slots(const t3_psx_part_t parts[T3_PSX_DRAW_PARTS],
                         const t3_psx_joint_t in[T3_PSX_DRAW_PARTS],uint32_t valid_parts,
                         t3_psx_joint_t out[T3_PSX_DRAW_PARTS],uint32_t* valid_slots) {
    t3_psx_joint_t result[T3_PSX_DRAW_PARTS]={0};uint32_t mask=0;
    if(!parts || !in || !out || !valid_slots || (valid_parts>>T3_PSX_DRAW_PARTS))return 0;
    uint32_t valid=valid_parts;
    for(unsigned i=0;i<T3_PSX_DRAW_PARTS;++i,valid>>=1)if(valid&1u) {
        unsigned slot=parts[i].slot;
        if(slot>=T3_PSX_DRAW_PARTS || (mask&t3_psx_bit(slot)))return 0;
        result[slot]=in[i];mask|=t3_psx_bit(slot);
    }
    for(unsigned i=0;i<T3_PSX_DRAW_PARTS;++i)out[i]=result[i];
    *valid_slots=mask;return 1;
}
unsigned t3_psx_count_parts(uint32_t mask) {
    unsigned n=0u;
    while(mask){n+=(unsigned)(mask&1u);mask>>=1u;}
    return n;
}
