#include "compiled_renderer.h"
#include <limits.h>

static uint32_t position_l1(const int16_t* p) {
    uint32_t sum=0;
    for(unsigned i=0;i<3u;++i)sum+=(uint32_t)(p[i]<0?-(int32_t)p[i]:p[i]);
    return sum;
}

int t3_compiled_renderer_init(t3_compiled_renderer_t* out,
    const t3_compiled_plan_t* p, const sat_skin_asset_t* s,
    const uint16_t* indices, uint16_t face_count, const uint8_t* double_sided) {
    if(!out || !p || !s || !indices || !double_sided || !p->joint_offsets ||
       !p->rigid_vertices || !p->vertex_offsets || !p->vertex_faces ||
       !p->face_joints || !p->exact_skin_supported || s->pos_shift!=15u ||
       s->max_joint>=T3_PSX_VISIBLE_PARTS || !s->joint_count ||
       s->joint_count!=p->joint_count || p->joint_count>255u ||
       p->vertex_count!=(uint32_t)s->rigid_count+s->blend_count ||
       !p->vertex_count || !p->face_count || p->face_count!=face_count ||
       (s->rigid_count && !s->rigid) || (s->blend_count && !s->blend) ||
       p->maximum_l1>(INT32_MAX-1024u)/32768u) return 0;
    uint32_t maximum=0, mask=0;
    for(unsigned j=0;j<p->joint_count;++j) {
        if(p->joint_offsets[j]>p->joint_offsets[j+1u] ||
           p->joint_offsets[j+1u]>s->rigid_count)return 0;
        unsigned previous=0;
        for(unsigned k=p->joint_offsets[j];k<p->joint_offsets[j+1u];++k) {
            const unsigned v=p->rigid_vertices[k];
            if(v>=s->rigid_count || s->rigid[v].joint!=j ||
               (k>p->joint_offsets[j] && v<=previous))return 0;
            previous=v;
        }
    }
    if(p->joint_offsets[0] || p->joint_offsets[p->joint_count]!=s->rigid_count)return 0;
    for(unsigned v=0;v<p->vertex_count;++v) {
        if(v<s->rigid_count) {
            const sat_skin_rigid_t* r=&s->rigid[v];
            if(r->joint>=T3_PSX_VISIBLE_PARTS)return 0;
            mask|=1u<<r->joint;
            const uint32_t l1=position_l1(r->pos);
            if(l1>maximum)maximum=l1;
        } else {
            const sat_skin_blend_t* b=&s->blend[v-s->rigid_count];
            if(b->count!=2u || b->weight[0]!=16384u)return 0;
            for(unsigned k=0;k<2u;++k) {
                if(b->joint[k]>=T3_PSX_VISIBLE_PARTS || b->joint[k]>=s->joint_count)return 0;
                mask|=1u<<b->joint[k];
                const uint32_t l1=position_l1(b->pos[k]);
                if(l1>maximum)maximum=l1;
            }
        }
        if(p->vertex_offsets[v]>p->vertex_offsets[v+1u] ||
           p->vertex_offsets[v+1u]>p->dependency_count)return 0;
        unsigned previous=0;
        for(unsigned k=p->vertex_offsets[v];k<p->vertex_offsets[v+1u];++k) {
            const unsigned f=p->vertex_faces[k];
            if(f>=p->face_count || (k>p->vertex_offsets[v] && f<=previous))return 0;
            const uint16_t* q=indices+4u*f;
            if(q[0]!=v && q[1]!=v && q[2]!=v && q[3]!=v)return 0;
            previous=f;
        }
    }
    if(maximum!=p->maximum_l1 || mask!=p->joint_mask ||
       !(mask&(1u<<s->max_joint)) || (mask>>s->max_joint)>1u ||
       p->vertex_offsets[0] || p->vertex_offsets[p->vertex_count]!=p->dependency_count)return 0;
    for(unsigned f=0;f<p->face_count;++f) {
        const uint16_t* q=indices+4u*f;
        unsigned joint=255u;
        if(!double_sided[f] && q[0]<s->rigid_count)joint=s->rigid[q[0]].joint;
        for(unsigned c=0;c<4u;++c) {
            const unsigned v=q[c];
            if(v>=p->vertex_count)return 0;
            if(v>=s->rigid_count || s->rigid[v].joint!=joint)joint=255u;
            unsigned found=0;
            for(unsigned k=p->vertex_offsets[v];k<p->vertex_offsets[v+1u];++k)
                found|=p->vertex_faces[k]==f;
            if(!found)return 0;
        }
        if(p->face_joints[f]!=joint)return 0;
    }
    *out=(t3_compiled_renderer_t){s,p};
    return 1;
}

int t3_compiled_pose_pack(const t3_compiled_renderer_t* r,
    const t3_psx_joint_t* original, uint32_t valid,
    int32_t cx, int32_t cz, t3_compiled_pose_t* out) {
    if(!r || !r->plan || !r->skin || !original || !out ||
       (valid&r->plan->joint_mask)!=r->plan->joint_mask ||
       cx< -30000 || cx>30000 || cz< -30000 || cz>30000)return 0;
    for(unsigned j=0;j<T3_PSX_VISIBLE_PARTS;++j) {
        if(!(r->plan->joint_mask&(1u<<j)))continue;
        const t3_vec3i_t p=original[j].origin;
        if(p.x< -30000 || p.x>30000 || p.y< -30000 || p.y>30000 ||
           p.z< -30000 || p.z>30000)return 0;
    }
    for(unsigned j=0;j<T3_PSX_VISIBLE_PARTS;++j) {
        if(!(r->plan->joint_mask&(1u<<j)))continue;
        for(unsigned axis=0;axis<3u;++axis) {
            for(unsigned c=0;c<3u;++c) {
                const int sign=(axis==0u?1:-1)*(c==0u?1:-1);
                out->joint[j][axis*4u+c]=(int32_t)original[j].rotation.m[axis][c]*sign;
            }
        }
        out->joint[j][3]=original[j].origin.x*16;
        out->joint[j][7]=-original[j].origin.y*16;
        out->joint[j][11]=-original[j].origin.z*16;
    }
    out->center_x=cx*96;
    out->center_z=cz*96;
    return 1;
}

static inline sat_vec3_t transform(const int32_t* m,const int16_t* q) {
    /* Exact cancellation of the bridge's factor 16 and Q15 factor 2:
     * floor((sum((16*R)*(2*q))+32768)/65536)
     * == floor((sum(R*q)+1024)/2048). Signed >> is arithmetic on our
     * supported compilers. Immutable L1 <= 65535 proves every sum fits.
     * |result| <= 1,528,560 with translation, so a half-pair sum and the
     * subsequent scale-six/center translation also fit signed int32. */
    return (sat_vec3_t){
        ((m[0]*q[0]+m[1]*q[1]+m[2]*q[2]+1024)>>11)+m[3],
        ((m[4]*q[0]+m[5]*q[1]+m[6]*q[2]+1024)>>11)+m[7],
        ((m[8]*q[0]+m[9]*q[1]+m[10]*q[2]+1024)>>11)+m[11]};
}

static inline sat_vec3_t place(sat_vec3_t v,const t3_compiled_pose_t* p) {
    return (sat_vec3_t){v.x*6-p->center_x,v.y*6,v.z*6+p->center_z};
}

int t3_compiled_deform_range(const t3_compiled_renderer_t* r,
    const t3_compiled_pose_t* p, uint32_t first, uint32_t count,
    sat_vec3_t* out, uint32_t capacity) {
    if(!r || !r->plan || !r->skin || !p || !out ||
       capacity<r->plan->vertex_count || first>r->plan->vertex_count ||
       count>r->plan->vertex_count-first)return 0;
    const sat_skin_asset_t* s=r->skin;
    const uint32_t end=first+count;
    uint32_t v=first;
    for(;v<end && v<s->rigid_count;++v) {
        const sat_skin_rigid_t* a=&s->rigid[v];
        out[v]=place(transform(p->joint[a->joint],a->pos),p);
    }
    for(;v<end;++v) {
        const sat_skin_blend_t* a=&s->blend[v-s->rigid_count];
        const sat_vec3_t p0=transform(p->joint[a->joint[0]],a->pos[0]);
        const sat_vec3_t p1=transform(p->joint[a->joint[1]],a->pos[1]);
        out[v]=place((sat_vec3_t){(p0.x+p1.x)>>1,(p0.y+p1.y)>>1,(p0.z+p1.z)>>1},p);
    }
    return 1;
}
