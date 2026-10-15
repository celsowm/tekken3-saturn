#include "psx_shadow_combat.h"

static int bounded(int64_t v) {
    return v>=-T3_PSX_UNIT_LIMIT && v<=T3_PSX_UNIT_LIMIT;
}
static int root_valid(const t3_psx_root_t* root) {
    return root &&
        bounded(root->origin.x) && bounded(root->origin.y) &&
        bounded(root->origin.z) &&
        root->sin_q12>=-4096 && root->sin_q12<=4096 &&
        root->cos_q12>=-4096 && root->cos_q12<=4096;
}
static int part_to_world(const t3_vec3i_t* part,
                         const t3_psx_root_t* root,
                         t3_vec3i_t* out) {
    int64_t x,z,y;
    if(!part || !root_valid(root) ||
       !bounded(part->x)||!bounded(part->y)||!bounded(part->z))
        return 0;
    /* Integer basis matches t3_attack_to_world. The model's local pose
     * translations are transformed before the hurt radii are applied. */
    x=(int64_t)root->origin.x+
        (((int64_t)part->x*root->cos_q12-
          (int64_t)part->z*root->sin_q12)>>12);
    z=(int64_t)root->origin.z+
        (((int64_t)part->x*root->sin_q12+
          (int64_t)part->z*root->cos_q12)>>12);
    y=(int64_t)root->origin.y+part->y;
    if(!bounded(x)||!bounded(y)||!bounded(z))return 0;
    out->x=(int32_t)x;out->y=(int32_t)y;out->z=(int32_t)z;
    return 1;
}
static int zones(const t3_psx_shadow_actor_t* actor,
                 t3_hurt_cylinder_t out[T3_PSX_HURT_ZONES]) {
    t3_psx_joint_t transformed[T3_PSX_DRAW_PARTS];
    uint32_t mask;
    unsigned i;
    if(!actor->pose_available || !actor->parts ||
       (actor->parts_mask&T3_PSX_BODY_PART_MASK)!=T3_PSX_BODY_PART_MASK)
        return 0;
    /* Both root and 18 parts must be valid before publishing any zones. */
    mask=actor->parts_mask;
    for(i=0;i<T3_PSX_DRAW_PARTS;++i) {
        /* Only the first 18 draw parts contain collision bodies. */
        transformed[i].origin.x=0;
        transformed[i].origin.y=0;
        transformed[i].origin.z=0;
    }
    for(i=0;i<18u;++i) {
        if(!part_to_world(&actor->parts[i].origin,&actor->root,
                           &transformed[i].origin))return -1;
    }
    if(!t3_psx_hurt_from_parts(transformed,mask,
                               actor->use_large_hurt!=0,out))return -1;
    return 1;
}
void t3_psx_shadow_reset(t3_psx_shadow_state_t* state) {
    if(state)__builtin_memset(state,0,sizeof(*state));
}
int t3_psx_shadow_step(t3_psx_shadow_state_t* state,
                       const t3_psx_shadow_actor_t actors[2],
                       t3_psx_shadow_result_t out[2]) {
    t3_psx_shadow_state_t next;
    t3_psx_shadow_result_t result[2];
    t3_hurt_cylinder_t body[2][T3_PSX_HURT_ZONES];
    int ready[2];
    unsigned i;
    if(!state || !actors || !out) return 0;
    next=*state;
    for(i=0;i<2u;++i) {
        /* Fail closed if a caller has lost its original PS1 placement.
         * Do not retain a stale previous-root frame across missing data. */
        ready[i]=zones(&actors[i],body[i]);
        result[i].status=T3_SHADOW_INACTIVE;
        result[i].new_contact=0u;
        result[i].zone=-1;
        result[i].segment=-1;
    }
    for(i=0;i<2u;++i) {
        const t3_psx_shadow_actor_t* actor=&actors[i];
        t3_psx_shadow_result_t* item=&result[i];
        t3_world_attack_t strike;
        t3_psx_contact_result_t contact;
        int swept;
        if(!actor->attack || !actor->attack->segment_count) {
            __builtin_memset(&next.history[i],0,sizeof(next.history[i]));
            next.contact_last[i]=0;
            continue;
        }
        if(actor->attack->segment_count>2u ||
           !root_valid(&actor->root)) {
            item->status=T3_SHADOW_BAD;
            __builtin_memset(&next.history[i],0,sizeof(next.history[i]));
            next.contact_last[i]=0;
            continue;
        }
        /* If the move changes, repeated world endpoints are not reused. */
        if(next.contact_last[i] && actor->move_slot!=next.last_slot[i])
            next.contact_last[i]=0;
        swept=t3_psx_sweep_sample(actor->attack,actor->move_slot,
                                  actor->attack_frame,&actor->root,
                                  next.root_valid[i]?&next.last_root[i]:NULL,
                                  &next.history[i],&strike);
        if(swept!=1) {
            item->status=swept<0?T3_SHADOW_BAD:T3_SHADOW_MISSING;
            next.contact_last[i]=0;
            if(swept<0)__builtin_memset(&next.history[i],0,sizeof(next.history[i]));
            continue;
        }
        if(ready[i^1u]!=1) {
            item->status=ready[i^1u]<0?T3_SHADOW_BAD:T3_SHADOW_MISSING;
            next.contact_last[i]=0;
            continue;
        }
        if(!t3_psx_contact_probe(&strike,body[i^1u],&contact)) {
            item->status=T3_SHADOW_BAD;
            next.contact_last[i]=0;
            continue;
        }
        if(contact.zone>=0) {
            item->status=T3_SHADOW_CONTACT;
            item->zone=contact.zone;
            item->segment=(int8_t)contact.segment;
            ++next.contact_frames[i];
            if(!next.contact_last[i]) {
                ++next.contact_edges[i];
                item->new_contact=1u;
            }
            next.contact_last[i]=1u;
        } else {
            item->status=T3_SHADOW_MISS;
            next.contact_last[i]=0u;
        }
        next.last_slot[i]=actor->move_slot;
    }
    for(i=0;i<2u;++i) {
        if(root_valid(&actors[i].root)) {
            next.last_root[i]=actors[i].root;
            next.root_valid[i]=1u;
        } else {
            next.root_valid[i]=0u;
            __builtin_memset(&next.history[i],0,sizeof(next.history[i]));
        }
    }
    *state=next;
    for(i=0;i<2u;++i)out[i]=result[i];
    return 1;
}
