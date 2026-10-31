#include "psx_contact.h"

/* PS1 zone table is keyed by the first 18 original joint-slot indices. This list
 * matches the independent game's hurt-zone-joints lookup.
 * Keep this mapping adjacent to the 14-zone generator: no cast from
 * KMD mesh order or arbitrary LibSaturn skeleton/GLB indices is safe here. */
static const uint8_t body_parts[T3_PSX_HURT_ZONES] =
    {17u,14u,10u,6u,16u,13u,9u,5u,2u,8u,4u,0u,15u,12u};

int t3_psx_hurt_profile(unsigned character) {
    /* g_hurtRadiusByChar: Kuma and Gun Jack are large; Gon and True Ogre have their own head. */
    switch(character) {
        case 11u: case 16u: return 1;
        case 17u: return 2;
        case 20u: return 3;
        default: return 0;
    }
}
int t3_psx_hurt_from_parts(const t3_psx_joint_t parts[T3_PSX_DRAW_PARTS],
                           uint32_t valid_mask, int large,
                           t3_hurt_cylinder_t out[T3_PSX_HURT_ZONES]) {
    t3_vec3i_t joints[T3_PSX_JOINT_COUNT]={0};
    t3_hurt_cylinder_t result[T3_PSX_HURT_ZONES];
    unsigned i;
    if(!parts || !out || (valid_mask&T3_PSX_BODY_PART_MASK)!=T3_PSX_BODY_PART_MASK)
        return 0;
    for(i=0;i<T3_PSX_HURT_ZONES;++i) {
        unsigned joint=body_parts[i];
        joints[joint]=parts[joint].origin;
    }
    if(!t3_hurt_from_psx_joints(joints,large,result))return 0;
    for(i=0;i<T3_PSX_HURT_ZONES;++i)out[i]=result[i];
    return 1;
}
static int needs_history(unsigned layout,unsigned segment,unsigned frame) {
    if(!frame)return 0;
    if(segment==0u)
        return layout==1u||layout==2u||layout==4u||
               layout==6u||layout==7u;
    return layout==2u||layout==3u||layout==9u;
}
int t3_psx_sweep_sample(const t3_attack_sample_t* sample,
                        uint16_t move_slot,unsigned frame,
                        const t3_psx_root_t* current,
                        const t3_psx_root_t* previous_root,
                        t3_psx_sweep_history_t* history,
                        t3_world_attack_t* out) {
    t3_world_attack_t result;
    t3_psx_sweep_history_t next;
    unsigned i;
    if(!sample || !history || !out || !current ||
       sample->segment_count==0u || sample->segment_count>2u ||
       frame>65535u)return -1;
    for(i=0;i<sample->segment_count;++i) {
        if(needs_history(sample->layout_kind,i,frame) &&
           (!history->valid || history->move_slot!=move_slot ||
            history->segment_count<=i ||
            (unsigned)history->last_frame+1u!=frame))
            return 0;
    }
    /* A continuous sweep uses the PREVIOUS WORLD endpoint verbatim, so
     * its old local start does not require yesterday's root transform.
     * Only the initial authored frame truly requires previous_root. */
    if(!t3_attack_to_world(sample,current,
                           frame?current:previous_root,&result))
        return 0;
    for(i=0;i<sample->segment_count;++i) {
        if(needs_history(sample->layout_kind,i,frame))
            result.segments[i].start=history->previous[i].end;
        result.segments[i].previous_start=
            (uint8_t)needs_history(sample->layout_kind,i,frame);
    }
    next=*history;
    next.valid=1u;
    next.move_slot=move_slot;
    next.last_frame=(uint16_t)frame;
    next.segment_count=result.segment_count;
    for(i=0;i<result.segment_count;++i)next.previous[i]=result.segments[i];
    *out=result;
    *history=next;
    return 1;
}
int t3_psx_contact_probe(const t3_world_attack_t* attack,
                         const t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES],
                         t3_psx_contact_result_t* result) {
    t3_psx_contact_result_t temp={0};
    unsigned segment=0u;
    int zone;
    if(!attack || !zones || !result)return 0;
    zone=t3_attack_hits_zones(attack,zones,T3_PSX_HURT_ZONES,&segment);
    if(zone< -1)return 0;
    temp.evaluated=1u;
    temp.zone=(int8_t)zone;
    temp.segment=(uint8_t)(zone>=0?segment:0u);
    *result=temp;
    return 1;
}
