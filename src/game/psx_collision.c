#include "psx_collision.h"
#include <limits.h>

/* The game runs in an arena much smaller than +/-1,000,000. Validate
 * external transformed data so squared distances never overflow int64_t. */
static int32_t in_bounds(int64_t x) {
    return x>=-T3_PSX_UNIT_LIMIT && x<=T3_PSX_UNIT_LIMIT;
}
static int valid(const t3_vec3i_t* p) {
    return p && in_bounds(p->x) && in_bounds(p->y) && in_bounds(p->z);
}
static int root_valid(const t3_psx_root_t* root) {
    return root && valid(&root->origin) &&
           root->sin_q12>=-4096 && root->sin_q12<=4096 &&
           root->cos_q12>=-4096 && root->cos_q12<=4096;
}
static int transform(t3_attack_point_t p, const t3_psx_root_t* root,
                     t3_vec3i_t* out) {
    int64_t x,z,y;
    if (!root_valid(root) || !out) return 0;
    x=(int64_t)root->origin.x+
      (((int64_t)p.x*root->cos_q12-(int64_t)p.z*root->sin_q12)>>12);
    z=(int64_t)root->origin.z+
      (((int64_t)p.x*root->sin_q12+(int64_t)p.z*root->cos_q12)>>12);
    y=(int64_t)root->origin.y+p.y;
    if (!in_bounds(x) || !in_bounds(y) || !in_bounds(z)) return 0;
    out->x=(int32_t)x; out->y=(int32_t)y; out->z=(int32_t)z;
    return 1;
}
int t3_attack_to_world(const t3_attack_sample_t* input,
                       const t3_psx_root_t* cur, const t3_psx_root_t* prev,
                       t3_world_attack_t* out) {
    unsigned i;
    t3_world_attack_t value={0};
    if (!input || !out || !root_valid(cur) ||
        input->segment_count>2u) return 0;
    value.segment_count=input->segment_count;
    for (i=0;i<value.segment_count;++i) {
        const t3_attack_segment_t* seg=&input->segments[i];
        const t3_psx_root_t* start=seg->previous_start?prev:cur;
        if (!transform(seg->start,start,&value.segments[i].start) ||
            !transform(seg->end,cur,&value.segments[i].end)) return 0;
        value.segments[i].previous_start=seg->previous_start;
    }
    *out=value;
    return 1;
}
static int64_t clamp(int64_t t,int64_t lo,int64_t hi) {
    return t<lo?lo:(t>hi?hi:t);
}
static int64_t at(int32_t origin, int64_t delta, int64_t t) {
    return (int64_t)origin+delta*t/65536;
}
int t3_segment_hits_hurt(const t3_world_segment_t* seg,
                         const t3_hurt_cylinder_t* c) {
    int64_t dx,dy,dz,low,high,lo=0,hi=65536,den,t,x,z,sx,sz;
    int64_t cx,cz,r2;
    if (!seg || !c || !valid(&seg->start) || !valid(&seg->end) ||
        !valid(&c->center)) return -1;
    if (!c->radius) return 0;
    dx=(int64_t)seg->end.x-seg->start.x;
    dy=(int64_t)seg->end.y-seg->start.y;
    dz=(int64_t)seg->end.z-seg->start.z;
    low=(int64_t)c->center.y-c->radius;
    high=(int64_t)c->center.y+c->radius;
    if (seg->start.y>high && seg->end.y>high) return 0;
    if (seg->start.y<low && seg->end.y<low) return 0;
    if (dy==0) {
        if (seg->start.y<low || seg->start.y>high) return 0;
    } else {
        /* Clip line to vertical slab, with one Q16 step expansion for
         * truncation at a slab boundary. */
        int64_t a=((low-seg->start.y)*65536)/dy;
        int64_t b=((high-seg->start.y)*65536)/dy;
        if (a>b) {int64_t temp=a;a=b;b=temp;}
        if (a>lo) lo=a>0?a-1:a;
        if (b<hi) hi=b<65536?b+1:b;
        lo=clamp(lo,0,65536);hi=clamp(hi,0,65536);
        if (lo>hi) return 0;
    }
    cx=(int64_t)c->center.x-seg->start.x;
    cz=(int64_t)c->center.z-seg->start.z;
    den=dx*dx+dz*dz; /* +/- 1 million bounded -> <=8e12 */
    if (den) {
        /* dot<=8e12; scaled by 65536 still fits signed int64_t. */
        t=(cx*dx+cz*dz)*65536/den;
        t=clamp(t,lo,hi);
    } else t=lo;
    sx=at(seg->start.x,dx,t)-c->center.x;
    sz=at(seg->start.z,dz,t)-c->center.z;
    x=sx*sx;z=sz*sz;
    r2=(int64_t)c->radius*c->radius;
    return x+z<=r2;
}
int t3_hurt_from_psx_joints(const t3_vec3i_t joints[T3_PSX_JOINT_COUNT],
                            int large, t3_hurt_cylinder_t out[T3_PSX_HURT_ZONES]) {
    static const uint8_t joint[14]=
        {17,14,10,6,16,13,9,5,2,8,4,0,15,12};
    static const uint16_t standard[14]=
        {200,200,120,120,300,300,100,100,250,300,300,200,400,400};
    static const uint16_t bigger[14]=
        {300,300,200,200,450,450,150,150,450,450,450,400,600,600};
    t3_hurt_cylinder_t temp[T3_PSX_HURT_ZONES];
    unsigned i;
    if (!joints || !out) return 0;
    for (i=0;i<T3_PSX_JOINT_COUNT;++i)
        if (!valid(&joints[i])) return 0;
    for (i=0;i<T3_PSX_HURT_ZONES;++i) {
        temp[i].center=joints[joint[i]];
        temp[i].radius=large?bigger[i]:standard[i];
    }
    temp[8].center.y-=120;
    temp[11].center.y-=60;
    if (!valid(&temp[8].center) || !valid(&temp[11].center))return 0;
    for(i=0;i<T3_PSX_HURT_ZONES;++i)out[i]=temp[i];
    return 1;
}
int t3_attack_hits_zones(const t3_world_attack_t* attack,
                          const t3_hurt_cylinder_t* zones,
                          size_t count, unsigned* segment_index) {
    size_t j; unsigned i;
    if (!attack || !zones || attack->segment_count>2u ||
        count>T3_PSX_HURT_ZONES) return -2;
    for(i=0;i<attack->segment_count;++i)
        for(j=0;j<count;++j) {
            int hit=t3_segment_hits_hurt(&attack->segments[i],&zones[j]);
            if(hit<0)return -2;
            if(hit) {
                if(segment_index)*segment_index=i;
                return (int)j;
            }
        }
    return -1;
}
