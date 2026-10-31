#include "psx_effect_fire.h"
#include <assert.h>
#include <string.h>

/* The fire objects' life cycles on a flat table: no sine, a known RNG. */
static uint8_t g_trig[0x2800],g_tables[T3_PSX_EFFECT_TABLE_BYTES];
static t3_psx_spark_t* only(t3_psx_effect_pool_t* p,unsigned type) {
    for(unsigned i=p->head;i!=T3_PSX_EFFECT_NONE;i=p->next[i])if(p->object[i].type==type)return &p->object[i];
    return NULL;
}
static unsigned count(const t3_psx_effect_pool_t* p,unsigned type) {
    unsigned n=0;
    for(unsigned i=p->head;i!=T3_PSX_EFFECT_NONE;i=p->next[i])n+=p->object[i].type==type;
    return n;
}
int main(void) {
    const t3_psx_trig_t trig={g_trig,sizeof g_trig};
    t3_psx_joint_t world[T3_PSX_DRAW_PARTS];memset(world,0,sizeof world);
    world[4].origin=(t3_vec3i_t){11,-22,33};
    t3_psx_effect_actor_t actors[2];memset(actors,0,sizeof actors);
    actors[0].world=actors[1].world=world;
    t3_psx_effect_pool_t pool;memset(&pool,0,sizeof pool);
    t3_psx_effect_pool_round(&pool);
    uint32_t rng=1;
    /* A missing table refuses instead of reading past it. */
    t3_psx_spark_t* o=t3_psx_effect_alloc(&pool,8);
    assert(o && !t3_psx_effect_fire_step(&pool,o,&trig,g_tables,440,actors,&rng,0,0));
    /* A flame publishes a segment from its previous position until its owner stops. */
    o->flags|=0x10;o->x=100;
    assert(t3_psx_effect_fire_step(&pool,o,&trig,g_tables,sizeof g_tables,actors,&rng,0,0));
    assert(o->state==1 && (o->flags&2) && (o->flags&0x40) && o->model==3 && o->px==100);
    actors[0].first_joint=0x1A;actors[0].pose=5;actors[0].active_first=1;
    assert(t3_psx_effect_fire_step(&pool,o,&trig,g_tables,sizeof g_tables,actors,&rng,0,0) && (o->flags&2));
    actors[0].first_joint=0;
    assert(t3_psx_effect_fire_step(&pool,o,&trig,g_tables,sizeof g_tables,actors,&rng,0,0) && !(o->flags&2));
    /* A clean hit on the target ends a live flame at once. */
    o->flags|=2;actors[1].hit_clean=1;actors[0].first_joint=0x1A;
    assert(t3_psx_effect_fire_step(&pool,o,&trig,g_tables,sizeof g_tables,actors,&rng,0,0));
    assert(o->type==0 && o->flags==0x80);
    actors[1].hit_clean=0;
    /* Gon burns one flame and marks the player; everyone else ten. */
    g_tables[T3_PSX_EFFECT_BURN_JOINTS]=4;
    t3_psx_effect_burn(&pool,g_tables,1,1);
    assert(count(&pool,11)==1 && pool.burn_flags==8);
    o=only(&pool,11);assert(o->d==4 && (o->flags&8) && (o->flags&0x20));
    /* A burning flame follows its joint and leaves after its time. */
    assert(t3_psx_effect_fire_step(&pool,o,&trig,g_tables,sizeof g_tables,actors,&rng,0,0));
    assert(o->x==11 && o->y==-22 && o->z==33 && o->state==1);
    o->a=0;
    assert(t3_psx_effect_fire_step(&pool,o,&trig,g_tables,sizeof g_tables,actors,&rng,0,0) && o->state==2);
    for(unsigned i=0;i<16 && o->type;++i)
        assert(t3_psx_effect_fire_step(&pool,o,&trig,g_tables,sizeof g_tables,actors,&rng,0,0));
    assert(o->type==0);
    t3_psx_effect_burn(&pool,g_tables,0,0);
    assert(count(&pool,11)==10);
    return 0;
}
