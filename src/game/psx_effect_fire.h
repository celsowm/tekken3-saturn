#ifndef T3_PSX_EFFECT_FIRE_H
#define T3_PSX_EFFECT_FIRE_H
#include "psx_effect_pool.h"
/* Object types 6-11, 19 and 20: fire breath, breath clouds, Gon's flame and gas,
 * their smoke and floor fire, and a burning body. Simulation and RNG are the
 * original's; the draw reuses the glow sprites. Returns 0 on missing tables. */
int t3_psx_effect_fire_step(t3_psx_effect_pool_t* pool,t3_psx_spark_t* object,const t3_psx_trig_t* trig,
    const uint8_t* tables,size_t bytes,const t3_psx_effect_actor_t actors[2],uint32_t* random,unsigned buffer,int replay);
/* FUN_80076F10's burn: flames on `player`, whose attacker is `gon` or not. */
void t3_psx_effect_burn(t3_psx_effect_pool_t* pool,const uint8_t* tables,unsigned player,int gon);
#endif
