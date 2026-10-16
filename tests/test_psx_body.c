#include "psx_body.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
static uint8_t trig[0x2800],sqrt_bytes[384],atan_bytes[1025],shapes[48];
static void p16(uint8_t* p,unsigned n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static unsigned isqrt(unsigned n) {
    unsigned lo=0,hi=65536;
    while(lo+1<hi){unsigned m=(lo+hi)/2;if(m*m<=n)lo=m;else hi=m;}
    return lo;
}
int main(void) {
    t3_psx_body_tables_t t={0};t3_psx_body_actor_t a[2]={0},before[2];
    uint32_t n; t3_psx_joint_t parts[T3_PSX_DRAW_PARTS]={0};t3_move_record_t move={0};
    for(unsigned i=0;i<192;++i)p16(sqrt_bytes+2*i,isqrt((i+64)<<18));
    p16(trig+0x800,4096);p16(trig+0x1800,(uint16_t)-4096);
    t.ground.trig=(t3_psx_trig_t){trig,sizeof(trig)};
    t.ground.atan=atan_bytes;t.ground.atan_size=sizeof(atan_bytes);
    t.sqrt=sqrt_bytes;t.sqrt_size=sizeof(sqrt_bytes);
    assert(t3_psx_body_sqrt(&t,0,&n) && n==0);
    assert(t3_psx_body_sqrt(&t,10000,&n) && n==99);
    n=33;assert(!t3_psx_body_sqrt(&t,0x80000000u,&n) && n==33);
    for(unsigned i=0;i<2;++i) {
        a[i].active=1;a[i].facing=i?0xC000:0x4000;
        a[i].anchor=a[i].root=a[i].previous_root=(t3_vec3i_t){i?50:-50,0,0};
        a[i].spheres[0]=(t3_psx_body_sphere_t){a[i].root,100};
    }
    before[0]=a[0];before[1]=a[1];
    assert(t3_psx_body_separate(a,100,&t));
    assert(a[0].root.x==-75 && a[1].root.x==75);
    assert(a[0].anchor.x==-75 && a[0].push.x==-25 && a[1].push.x==25);
    assert(a[0].contact && a[0].contact_with==2 && a[1].contact_with==1);
    a[0]=before[0];a[1]=before[1];a[0].active_first=a[1].active_first=1;
    assert(t3_psx_body_separate(a,100,&t));
    assert(a[0].root.x==-250 && a[1].root.x==250); /* charge minimum, no halving */
    a[0]=before[0];a[1]=before[1];a[0].invulnerable=1;
    before[0]=a[0];before[1]=a[1];
    assert(t3_psx_body_separate(a,100,&t) && !memcmp(a,before,sizeof(a)));
    a[0].invulnerable=0;t.sqrt_size=0;before[0]=a[0];
    assert(!t3_psx_body_separate(a,100,&t) && !memcmp(a,before,sizeof(a)));
    t.sqrt_size=sizeof(sqrt_bytes);
    a[0].root.x=a[0].anchor.x=300100;a[0].ramp=50;
    assert(t3_psx_arena_bounds(&a[0],&a[1],&t));
    assert(a[0].root.x==300050 && a[0].anchor.x==300050);
    for(unsigned k=0;k<8;++k){shapes[4*k]=(uint8_t)k;p16(shapes+32+2*k,100);parts[k].origin.x=(int32_t)k;}
    move.flags=0x20000u;
    assert(t3_psx_body_shapes(&a[0],parts,0xFF,shapes,&move,1,0,0));
    assert(a[0].spheres[3].radius==100 && a[0].spheres[4].radius==0);
    move.active_first=3;move.active_last=6;move.flags=0;move.body_profile=6;
    assert(t3_psx_body_shapes(&a[0],parts,0xFF,shapes,&move,4,0,0));
    assert(a[0].spheres[0].radius==160 && a[0].spheres[1].radius==0 && a[0].spheres[4].radius==1);
    before[0]=a[0];shapes[0]=30;
    assert(!t3_psx_body_shapes(&a[0],parts,0xFF,shapes,&move,4,0,0));
    assert(!memcmp(&a[0],&before[0],sizeof(a[0])));
    return 0;
}
