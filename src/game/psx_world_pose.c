#include "psx_world_pose.h"

void t3_psx_world_pose_cache_init(t3_psx_world_pose_cache_t* c,
    t3_psx_world_pose_entry_t* entries,unsigned capacity) {
    if(!c)return;
    *c=(t3_psx_world_pose_cache_t){.entries=entries,
        .capacity=entries && capacity>=4u && !(capacity&(capacity-1u))?capacity:0u};
    for(unsigned i=0;i<c->capacity;++i)entries[i].valid=0;
}
static uint32_t hash_matrix(uint32_t hash,const t3_psx_matrix_t* m) {
    for(unsigned r=0;r<3;++r)for(unsigned col=0;col<3;++col)
        hash=(hash^(uint16_t)m->m[r][col])*16777619u;
    return hash;
}
static int matrix_equal(const t3_psx_matrix_t* a,const t3_psx_matrix_t* b) {
    for(unsigned r=0;r<3;++r)for(unsigned col=0;col<3;++col)
        if(a->m[r][col]!=b->m[r][col])return 0;
    return 1;
}
static int same(const t3_psx_world_pose_entry_t* e,const t3_psx_part_t* parts,
    const t3_psx_matrix_t* slots,uint32_t mask,const t3_psx_matrix_t* root,uint32_t hash) {
    if(!e->valid || e->fingerprint!=hash || e->parts!=parts || e->slots_mask!=mask ||
       !matrix_equal(&e->root,root))return 0;
    /* Masks are walked by shifting: the SH-2 has no variable shift. */
    uint32_t m=mask;
    for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i,m>>=1)
        if((m&1u) && !matrix_equal(&e->slots[i],&slots[i]))return 0;
    return 1;
}
static int valid_origin(t3_vec3i_t p) {
    return p.x>=-T3_PSX_UNIT_LIMIT && p.x<=T3_PSX_UNIT_LIMIT &&
        p.y>=-T3_PSX_UNIT_LIMIT && p.y<=T3_PSX_UNIT_LIMIT &&
        p.z>=-T3_PSX_UNIT_LIMIT && p.z<=T3_PSX_UNIT_LIMIT;
}
int t3_psx_compose_cached(const t3_psx_part_t parts[T3_PSX_DRAW_PARTS],
    const t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS],uint32_t mask,
    const t3_psx_joint_t* root,t3_psx_world_pose_cache_t* c,
    t3_psx_joint_t out[T3_PSX_DRAW_PARTS],uint32_t* valid_parts) {
    if(!parts || !slots || !root || !out || !valid_parts ||
       (mask>>T3_PSX_ROTATION_SLOTS) || !valid_origin(root->origin))return 0;
    if(!c || !c->capacity)return t3_psx_compose_parts(parts,slots,mask,root,out,valid_parts);
    uint32_t hash=hash_matrix(2166136261u^mask,&root->rotation);
    uint32_t m=mask;
    for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i,m>>=1)
        if(m&1u)hash=hash_matrix(hash,&slots[i]);
    const unsigned first=(hash&((c->capacity/4u)-1u))*4u;
    t3_psx_world_pose_entry_t* victim=&c->entries[first];
    ++c->clock;
    for(unsigned i=0;i<4u;++i) {
        t3_psx_world_pose_entry_t* e=&c->entries[first+i];
        if(same(e,parts,slots,mask,&root->rotation,hash)) {
            /* For each original part, origin = root.origin + relative.
             * Rotations and floor12(parent.rotation * offset) do not depend
             * on root translation. Checking every valid node also checks
             * every intermediate parent, as in the original traversal.
             * All checks precede the first write to `out`. */
            m=e->parts_mask;
            for(unsigned p=0;p<T3_PSX_DRAW_PARTS;++p,m>>=1)if(m&1u) {
                const int64_t x=(int64_t)e->relative[p].origin.x+root->origin.x;
                const int64_t y=(int64_t)e->relative[p].origin.y+root->origin.y;
                const int64_t z=(int64_t)e->relative[p].origin.z+root->origin.z;
                if(x< -T3_PSX_UNIT_LIMIT || x>T3_PSX_UNIT_LIMIT ||
                   y< -T3_PSX_UNIT_LIMIT || y>T3_PSX_UNIT_LIMIT ||
                   z< -T3_PSX_UNIT_LIMIT || z>T3_PSX_UNIT_LIMIT)return 0;
            }
            __builtin_memcpy(out,e->relative,sizeof(e->relative));
            m=e->parts_mask;
            for(unsigned p=0;p<T3_PSX_DRAW_PARTS;++p,m>>=1)if(m&1u) {
                out[p].origin.x+=root->origin.x;
                out[p].origin.y+=root->origin.y;
                out[p].origin.z+=root->origin.z;
            }
            *valid_parts=e->parts_mask;
            e->stamp=c->clock;++c->hits;return 1;
        }
        if(!e->valid || (victim->valid && c->clock-e->stamp>c->clock-victim->stamp))victim=e;
    }
    ++c->misses;
    if(!t3_psx_compose_parts(parts,slots,mask,root,out,valid_parts))return 0;
    if(victim->valid)++c->evictions;
    victim->valid=0;victim->parts=parts;victim->fingerprint=hash;
    victim->slots_mask=mask;victim->parts_mask=*valid_parts;victim->root=root->rotation;
    m=mask;
    for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i,m>>=1)
        if(m&1u)t3_psx_matrix_copy(&victim->slots[i],&slots[i]);
    __builtin_memcpy(victim->relative,out,sizeof(victim->relative));
    m=*valid_parts;
    for(unsigned i=0;i<T3_PSX_DRAW_PARTS;++i,m>>=1)if(m&1u) {
        victim->relative[i].origin.x-=root->origin.x;
        victim->relative[i].origin.y-=root->origin.y;
        victim->relative[i].origin.z-=root->origin.z;
    }
    victim->stamp=c->clock;victim->valid=1;
    return 1;
}
