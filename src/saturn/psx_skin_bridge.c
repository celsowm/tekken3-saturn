#include "psx_skin_bridge.h"

static int bounded(t3_vec3i_t p) {
    /* Both coordinate and Q16 matrix storage are bounded. This is much
     * stricter than the core's generic +/-1,000,000 analysis limit. */
    return p.x >= -30000 && p.x <= 30000 &&
           p.y >= -30000 && p.y <= 30000 &&
           p.z >= -30000 && p.z <= 30000;
}
int t3_psx_skin_bridge(const t3_psx_joint_t pose[T3_PSX_DRAW_PARTS],
                       uint32_t valid_parts,
                       sat_mat4_t matrices[T3_PSX_DRAW_PARTS]) {
    sat_mat4_t result[T3_PSX_VISIBLE_PARTS];
    unsigned i,r,c;
    if (!pose || !matrices || (valid_parts & T3_PSX_VISIBLE_MASK)
                           != T3_PSX_VISIBLE_MASK) return 0;
    for (i=0;i<T3_PSX_VISIBLE_PARTS;++i) {
        const t3_psx_joint_t* j=&pose[i];
        sat_mat4_t* m=&result[i];
        if (!bounded(j->origin)) return 0;
        for (r=0;r<16u;++r)m->m[r]=0;
        for (r=0;r<3u;++r)
            for (c=0;c<3u;++c) {
                /* Signed Q12 -> 16.16 multiplication avoids undefined
                 * left shifts of negative values. Max abs <= 524288. */
                const int sign_r = r == 0u ? 1 : -1;
                const int sign_c = c == 0u ? 1 : -1;
                /* Same basis as the imported anchors: D * R * D^-1. */
                m->m[r*4u+c]=(sat_fx16_t)((int32_t)j->rotation.m[r][c]*16*sign_r*sign_c);
            }
        m->m[3]=(sat_fx16_t)(j->origin.x*16);
        m->m[7]=(sat_fx16_t)(-j->origin.y*16);
        m->m[11]=(sat_fx16_t)(-j->origin.z*16);
        m->m[15]=SAT_FX16_ONE;
    }
    for (i=0;i<T3_PSX_VISIBLE_PARTS;++i)matrices[i]=result[i];
    return 1;
}

int t3_psx_skin_placement(sat_skin_placement_t* placement,int32_t cx,int32_t cz) {
    if(!placement || cx< -30000 || cx>30000 || cz< -30000 || cz>30000)return 0;
    *placement=(sat_skin_placement_t){6,{-cx*96,0,cz*96},0x0FFFFFFF};
    return 1;
}

int t3_psx_skin_place(sat_vec3_t* vertices,uint32_t count,int32_t cx,int32_t cz) {
    if(!vertices || !count || cx< -30000 || cx>30000 || cz< -30000 || cz>30000)return 0;
    for(uint32_t i=0;i<count;++i) {
        const sat_vec3_t* v=&vertices[i];
        if(v->x< -0x0FFFFFFF || v->x>0x0FFFFFFF || v->y< -0x0FFFFFFF ||
           v->y>0x0FFFFFFF || v->z< -0x0FFFFFFF || v->z>0x0FFFFFFF)return 0;
    }
    /* Six is integral: all diagonal Q16 products and translations are exact.
     * The bounds above also prove every signed operation below fits int32. */
    for(uint32_t i=0;i<count;++i) {
        vertices[i].x=vertices[i].x*6-cx*96;
        vertices[i].y=vertices[i].y*6;
        vertices[i].z=vertices[i].z*6+cz*96;
    }
    return 1;
}
