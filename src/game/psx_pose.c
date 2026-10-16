#include "psx_pose.h"
#include "fixed_shift.h"
#include <stdint.h>

static const uint8_t k_slots[10]={0u,1u,11u,2u,3u,7u,6u,10u,14u,17u};

static int32_t floor12(int64_t v) {
    return t3_floor_shift(v,12u);
}
static int16_t sat16(int32_t v) {
    return (int16_t)(v>32767 ? 32767 : (v< -32768 ? -32768 : v));
}
static int16_t wrap16(int32_t v) {
    uint16_t w=(uint16_t)v;
    return (int16_t)(w<0x8000u?(int32_t)w:(int32_t)w-65536);
}
static int16_t gpf(int32_t a,int32_t b) {
    return sat16(floor12((int64_t)a*b));
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

static void euler(int16_t x,int16_t y,int16_t z,
                  const t3_psx_trig_t* trig,t3_psx_matrix_t* out) {
    /* Port of the original GTE sequence documented in
     * Tekken3Remake tools/research/pose.py EulerToMatrix.
     * GPF (IR) saturation happens at each intermediate multiply. */
    int16_t sx=sine(trig,(uint16_t)x,0u),cx=sine(trig,(uint16_t)x,1u);
    int16_t sy=sine(trig,(uint16_t)y,0u),cy=sine(trig,(uint16_t)y,1u);
    int16_t sz=sine(trig,(uint16_t)z,0u),cz=sine(trig,(uint16_t)z,1u);
    int16_t a1=gpf(sx,cz),a2=gpf(sx,sz),a3=gpf(sx,cy);
    int16_t b1=gpf(sy,a1),b2=gpf(sy,a2);
    int16_t cxcz=sat16(floor12((int64_t)cx*cz));
    int16_t b3=gpf(sy,cxcz),c1=gpf(cy,cz),c2=gpf(cy,sz),c3=gpf(cy,cx);
    int16_t cxsz=sat16(floor12((int64_t)cx*sz));
    out->m[2][1]=a3;
    out->m[2][0]=wrap16(-sy);
    out->m[0][1]=wrap16((int32_t)b1-cxsz);
    out->m[1][1]=wrap16((int32_t)cxcz+b2);
    out->m[0][2]=wrap16((int32_t)a2+b3);
    out->m[1][2]=wrap16(floor12((int64_t)cxsz*sy)-a1);
    out->m[0][0]=c1;
    out->m[1][0]=c2;
    out->m[2][2]=c3;
}
int t3_psx_pose_fk(const int16_t pose[T3_PSX_POSE_CHANNELS],
                   const t3_psx_trig_t* table,
                   t3_psx_matrix_t out[T3_PSX_ROTATION_SLOTS],
                   uint32_t* valid_slots) {
    t3_psx_matrix_t result[T3_PSX_ROTATION_SLOTS]={0};
    uint32_t mask=0u;
    unsigned i;
    if (!pose || !valid_table(table) || !out || !valid_slots) return 0;
    for(i=0;i<10u;++i) {
        unsigned ch=19u+3u*i;
        euler(pose[ch],pose[ch+1u],pose[ch+2u],table,&result[k_slots[i]]);
        mask|=(uint32_t)1u<<k_slots[i];
    }
    for(i=0;i<T3_PSX_ROTATION_SLOTS;++i)out[i]=result[i];
    *valid_slots=mask;
    return 1;
}
int t3_psx_mul_rot(const t3_psx_matrix_t* a,const t3_psx_matrix_t* b,
                   t3_psx_matrix_t* out) {
    t3_psx_matrix_t r={0};
    unsigned i,j,k;
    if(!a || !b || !out)return 0;
    for(j=0;j<3u;++j)
        for(i=0;i<3u;++i) {
            int64_t dot=0;
            for(k=0;k<3u;++k)dot+=(int64_t)a->m[i][k]*b->m[k][j];
            r.m[i][j]=sat16(floor12(dot));
        }
    *out=r;
    return 1;
}
static int valid_vec(t3_vec3i_t p) {
    return p.x>=-T3_PSX_UNIT_LIMIT && p.x<=T3_PSX_UNIT_LIMIT &&
           p.y>=-T3_PSX_UNIT_LIMIT && p.y<=T3_PSX_UNIT_LIMIT &&
           p.z>=-T3_PSX_UNIT_LIMIT && p.z<=T3_PSX_UNIT_LIMIT;
}
static int place(const t3_psx_joint_t* parent,const t3_vec3i_t* delta,
                 t3_vec3i_t* out) {
    int64_t coords[3];
    int64_t x[3]={(int64_t)delta->x,delta->y,delta->z};
    unsigned i,j;
    for(i=0;i<3u;++i) {
        int64_t sum=0;
        for(j=0;j<3u;++j)sum+=(int64_t)parent->rotation.m[i][j]*x[j];
        coords[i]=(int64_t)(i==0u?parent->origin.x:
                (i==1u?parent->origin.y:parent->origin.z))+floor12(sum);
        if(coords[i]<-T3_PSX_UNIT_LIMIT || coords[i]>T3_PSX_UNIT_LIMIT)
            return 0;
    }
    out->x=(int32_t)coords[0];
    out->y=(int32_t)coords[1];
    out->z=(int32_t)coords[2];
    return 1;
}
int t3_psx_compose_parts(const t3_psx_part_t parts[T3_PSX_DRAW_PARTS],
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
           !(valid_slots & ((uint32_t)1u<<p->slot)))continue;
        if(p->parent>=0 && !(mask & ((uint32_t)1u<<p->parent)))continue;
        par=p->parent<0?root:&result[p->parent];
        if(!t3_psx_mul_rot(&par->rotation,&slots[p->slot],
                           &result[i].rotation) ||
           !place(par,&p->offset,&result[i].origin))return 0;
        mask|=(uint32_t)1u<<i;
    }
    for(i=0;i<T3_PSX_DRAW_PARTS;++i)out[i]=result[i];
    *valid_parts=mask;
    return 1;
}
int t3_psx_parts_to_slots(const t3_psx_part_t parts[T3_PSX_DRAW_PARTS],
                         const t3_psx_joint_t in[T3_PSX_DRAW_PARTS],uint32_t valid_parts,
                         t3_psx_joint_t out[T3_PSX_DRAW_PARTS],uint32_t* valid_slots) {
    t3_psx_joint_t result[T3_PSX_DRAW_PARTS]={0};uint32_t mask=0;
    if(!parts || !in || !out || !valid_slots || (valid_parts>>T3_PSX_DRAW_PARTS))return 0;
    for(unsigned i=0;i<T3_PSX_DRAW_PARTS;++i)if(valid_parts&(1u<<i)) {
        unsigned slot=parts[i].slot;
        if(slot>=T3_PSX_DRAW_PARTS || (mask&(1u<<slot)))return 0;
        result[slot]=in[i];mask|=1u<<slot;
    }
    for(unsigned i=0;i<T3_PSX_DRAW_PARTS;++i)out[i]=result[i];
    *valid_slots=mask;return 1;
}
unsigned t3_psx_count_parts(uint32_t mask) {
    unsigned n=0u;
    while(mask){n+=(unsigned)(mask&1u);mask>>=1u;}
    return n;
}
