/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Effect integration adapted from pinned Tekken3Remake fight_simulation.gd,
 * combat.gd and effect_objects.gd. Original draw order, hit-slot endpoint,
 * effect direction and two-spray ordering; caller-owned portable storage.
 * See NOTICE.md. */
#include "psx_match.h"
#include "attack_records.h"
#include "psx_effect_fire.h"
static int32_t add(int32_t a,int32_t b){return (int32_t)((uint32_t)a+(uint32_t)b);}
static int32_t trig(const t3_psx_trig_t* t,unsigned angle,unsigned cos) {
    unsigned at=2*(angle&4095)+(cos?2048:0);
    return (int16_t)(uint16_t)(t->data[at]|(t->data[at+1]<<8));
}
static void direction(const t3_psx_match_t* m,t3_vec3i_t d,int16_t* pitch,int16_t* yaw) {
    const t3_psx_camera_tables_t* t=m->config->camera->tables;
    const t3_psx_trig_t* tr=&m->config->ground_tables.trig;
    int32_t y=(t3_psx_camera_atan4096(t,d.x,d.z)-1024)&4095;
    int32_t xabs=d.x<0?-d.x:d.x,zabs=d.z<0?-d.z:d.z;
    int32_t n=zabs<xabs?d.x:d.z,s=trig(tr,(unsigned)y,zabs>=xabs);
    int16_t divisor=(int16_t)(s*8);if(!(s&0x1FFF))divisor=1;
    int32_t flat=((int32_t)((uint32_t)n<<16)/divisor)>>1;if(flat<0)flat=-flat;
    *yaw=(int16_t)y;*pitch=(int16_t)((t3_psx_camera_atan4096(t,d.y,flat)-1024)&4095);
}
/* FUN_8006E21C: an object at the first of two offsets from a joint, aimed at the second. */
static t3_psx_spark_t* emit(t3_psx_match_t* m,unsigned player,unsigned type,unsigned joint,unsigned offsets) {
    const t3_psx_joint_t* j=&m->fighter[player].world[joint];
    const uint8_t* off=m->config->effect_tables+offsets;
    t3_vec3i_t p[2];
    for(unsigned k=0;k<2;++k,off+=8) {
        const int32_t v[3]={(int16_t)(uint16_t)(off[0]|(off[1]<<8)),(int16_t)(uint16_t)(off[2]|(off[3]<<8)),
                            (int16_t)(uint16_t)(off[4]|(off[5]<<8))};
        p[k].x=add(j->origin.x,t3_psx_dot12(j->rotation.m[0],v));
        p[k].y=add(j->origin.y,t3_psx_dot12(j->rotation.m[1],v));
        p[k].z=add(j->origin.z,t3_psx_dot12(j->rotation.m[2],v));
    }
    t3_psx_spark_t* o=t3_psx_effect_alloc(m->config->effects,type);
    if(!o)return NULL;
    o->flags|=player?8u:0x10u;
    o->x=p[0].x;o->y=p[0].y;o->z=p[0].z;
    const t3_vec3i_t d={add(p[1].x,-p[0].x),add(p[1].y,-p[0].y),add(p[1].z,-p[0].z)};
    direction(m,d,&o->pitch,&o->yaw);
    return o;
}
/* The objects a running projectile descriptor emits (EffectObjects.descriptor_spawn). */
static void descriptor_spawn(t3_psx_match_t* m,unsigned i,const t3_psx_effect_actor_t* a) {

    if(m->config->effects->end_cleared || a->first_joint<0x18u || a->first_joint>0x1Cu)return;
    if(a->pose<a->active_first || a->active_last<a->pose)return;
    switch(a->first_joint) {
    case 0x18:(void)emit(m,i,6,2,T3_PSX_EFFECT_BREATH_OFFSETS+16u*i);break;
    case 0x19:case 0x1C: {
        t3_psx_spark_t* o=emit(m,i,10,2,T3_PSX_EFFECT_CLOUD_OFFSETS);
        if(o)o->d=a->first_joint==0x1Cu;
        break;
    }
    case 0x1A:(void)emit(m,i,8,2,T3_PSX_EFFECT_FLAME_OFFSETS);break;
    case 0x1B:
        if(a->pose==a->active_first)(void)emit(m,i,19,11,T3_PSX_EFFECT_GAS_OFFSETS);
        break;
    }
}
int t3_psx_match_effects_update(t3_psx_match_t* m,int replay) {
    const t3_psx_match_config_t* c=m->config;
    if(!c->effects)return 1;
    if(!c->camera)return 0;
    t3_psx_effect_actor_t actors[2]={0};
    for(unsigned i=0;i<2;++i) {
        const t3_psx_fighter_t* f=&m->fighter[i];
        actors[i]=(t3_psx_effect_actor_t){.active=f->body.active,.character=c->character[i],
            .hit_clean=f->hit.hit_clean,.guarded=f->hit.guarded,
            .power_timer=f->ground.power_timer,.pose=(int16_t)f->vm.clock.pose_frame,
            .active_first=f->vm.move.active_first,.active_last=f->vm.move.active_last,.world=f->world};
        const int found=t3_attack_descriptor_joints(f->vm.active_bank,&f->vm.move,actors[i].joints);
        if(found==1)actors[i].first_joint=actors[i].joints[0];
        else if(f->ground.power_timer)return 0;
    }
    if(!replay || !(m->replay.play&1)) {
        if(c->effect_table_bytes!=T3_PSX_EFFECT_TABLE_BYTES)return 0;
        descriptor_spawn(m,0,&actors[0]);descriptor_spawn(m,1,&actors[1]);
    }
    return t3_psx_effect_pool_step(c->effects,&c->ground_tables.trig,c->effect_tables,c->effect_table_bytes,
        actors,&m->camera.rand,m->frame_counter&1u,!replay || !(m->replay.play&1),replay,m->camera.phase);
}
int t3_psx_match_effects_hit(t3_psx_match_t* m,unsigned defender) {
    if(!m->config->effects)return 1;
    const t3_psx_fighter_t* f=&m->fighter[defender];
    if(!f->got_hit || f->hit.best_slot<0)return 1;
    const t3_psx_hit_candidate_t* slot=&f->hit.slots[(unsigned)f->hit.best_slot];
    if(!f->hit.hit_clean)return 1;
    int damage=f->hit.total_damage>slot->base_damage?f->hit.total_damage:slot->base_damage;
    unsigned a=1u-defender;const t3_psx_fighter_t* attacker=&m->fighter[a];
    {
        /* FUN_80076F10: the hit burst (set 3, index 0, drifting along the slot's vector) and,
         * from 21 damage, the attacker's own flipbook (stationary). State only. */
        const unsigned seg=(unsigned)attacker->geometry.segment;
        if(attacker->projectile_hit || seg<attacker->strike.segment_count) {
            const t3_world_segment_t* s0=attacker->projectile_hit?&attacker->projectile:&attacker->strike.segments[0];
            const t3_world_segment_t* s1=attacker->projectile_hit?&attacker->projectile:&attacker->strike.segments[seg];
            const int32_t at[3]={s1->end.x,s1->end.y,s1->end.z};
            const int16_t vec[3]={(int16_t)(s0->end.x-s0->start.x),(int16_t)(s0->end.y-s0->start.y),
                                  (int16_t)(s0->end.z-s0->start.z)};
            t3_psx_flipbook_pool_t* book=&m->config->effects->flipbook;
            t3_psx_flipbook_spawn(book,3,0,27,at,vec,0,attacker->side_flag);
            if(damage>=21) {
                const unsigned id=m->config->character[a];
                t3_psx_flipbook_spawn(book,a,0,(id==9||id==13||id==17||id==19)?22u:30u,at,vec,1,attacker->side_flag);
            }
        }
    }
    unsigned code=attacker->strike_joint;
    if(code==0x19 || code==0x1B || code==0x1C)return 1;
    if(code!=0x18 && code!=0x1A && damage<21)return 1;
    if(code==0x18 || code==0x1A){t3_psx_effect_burn(m->config->effects,m->config->effect_tables,defender,m->config->character[a]==17u);return 1;}
    unsigned segment=(unsigned)attacker->geometry.segment;
    if(segment>=attacker->strike.segment_count)return 0;
    t3_vec3i_t point=attacker->strike.segments[segment].end;
    const t3_world_segment_t* first=&attacker->strike.segments[0];
    t3_vec3i_t d={add(first->end.x,-first->start.x),add(first->end.y,-first->start.y),add(first->end.z,-first->start.z)};
    int16_t pitch,yaw;direction(m,d,&pitch,&yaw);
    t3_psx_effect_spray(m->config->effects,point,pitch,yaw,attacker->ground.power_timer!=0);
    return 1;
}
void t3_psx_match_effects_sound(void* user,unsigned kind,unsigned player,uint16_t id) {
    t3_psx_match_t* m=user;
    if(player>1)return;
    uint16_t code=id;
    const int voice=kind==T3_SOUND_VOICE && (id>>12)>=2 && (id>>12)<=5 &&
        t3_psx_sound_voice_code(m->config->sound_tables,m->config->sound_voice_set[player],id,&code)==1;
    if(m->config->sound_sink && (kind!=T3_SOUND_VOICE || voice))
        m->config->sound_sink(m->config->sound_sink_user,kind,player,code);
    if(!m->config->effects || kind!=T3_SOUND_VOICE || (id>>12)!=4 || !voice)return;
    if(m->fighter[player].ground.in_throw)return;
    t3_psx_spark_t* o=t3_psx_effect_alloc(m->config->effects,17);
    if(o){o->a=(int32_t)player;o->b=code;}
}
