#include "psx_particle_snapshot.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

int main(void) {
    t3_psx_effect_pool_t pool={0}; t3_psx_particle_snapshot_t image,before;
    t3_psx_effect_pool_round(&pool);
    /* The eight freshly allocated glows are not sprites until initialized. */
    assert(t3_psx_particle_snapshot(&image,&pool) && image.count==0);
    t3_psx_spark_t* a=t3_psx_effect_alloc(&pool,2);
    t3_psx_spark_t* b=t3_psx_effect_alloc(&pool,13);
    assert(a && b);
    a->flags=0xc0;a->model=1;a->x=-7;a->y=123;a->z=999;a->clut[0]=0x7f91;a->clut[1]=0x7f92;
    b->flags=0xe0;b->model=2;b->x=42;b->clut[0]=0x7f51;b->clut[1]=0x7f52;
    assert(t3_psx_particle_snapshot(&image,&pool) && image.count==2);
    assert(image.item[0].slot==8 && image.item[1].slot==9);
    assert(image.item[0].position[0]==-7 && image.item[0].position[1]==123 && image.item[0].position[2]==999);
    assert(image.item[0].clut[0]==0x7f91 && image.item[0].clut[1]==0x7f92);
    assert(image.item[1].model==2 && image.item[1].flags==0xe0);
    /* Snapshot is independent of subsequent worker mutation. */
    a->x=400;a->clut[0]=0;b->flags=0;
    assert(image.item[0].position[0]==-7 && image.item[0].clut[0]==0x7f91 && image.count==2);
    assert(t3_psx_particle_snapshot(&image,&pool) && image.count==1);
    for(unsigned n=10;n<80;++n) {
        t3_psx_spark_t* p=t3_psx_effect_alloc(&pool,1);assert(p);p->flags=0x40;p->model=1;
    }
    for(unsigned n=0;n<10;++n)pool.object[n].flags=0x40;
    assert(t3_psx_particle_snapshot(&image,&pool) && image.count==80);
    for(unsigned n=0;n<80;++n)assert(image.item[n].slot==n);
    before=image;pool.next[79]=0;
    assert(!t3_psx_particle_snapshot(&image,&pool) && !memcmp(&before,&image,sizeof image));
    pool.next[79]=80;pool.previous[40]=7;
    assert(!t3_psx_particle_snapshot(&image,&pool) && !memcmp(&before,&image,sizeof image));
    puts("particle snapshot: ok");return 0;
}
