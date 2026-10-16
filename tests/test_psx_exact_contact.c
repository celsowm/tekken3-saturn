#include "psx_exact_contact.h"
#include "psx_collision.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
static uint8_t bytes[T3_PSX_TRIG_BYTES];
static void p16(unsigned offset,int n){bytes[offset]=(uint8_t)n;bytes[offset+1]=(uint8_t)((unsigned)n>>8);}
int main(void) {
    t3_world_segment_t segment={{-1000,0,0},{1000,0,0},0};
    t3_hurt_cylinder_t cylinder={{0,0,0},10};
    t3_attack_sample_t sample={0};t3_world_attack_t out,before,previous={0};
    t3_psx_trig_t trig={bytes,sizeof(bytes)};
    /* Preserve the original's wrapped perpendicular-foot product. */
    assert(t3_segment_hits_hurt(&segment,&cylinder)==1);
    assert(t3_psx_segment_hits_cylinder(&segment,&cylinder)==0);
    segment.start.x=-500;segment.end.x=500;
    assert(t3_psx_segment_hits_cylinder(&segment,&cylinder)==1);
    segment.start.y=100;segment.end.y=100;
    assert(t3_psx_segment_hits_cylinder(&segment,&cylinder)==0);
    p16(0x800,4096);p16(0x1800,-4096);
    sample.segment_count=1;sample.segments[0].start=(t3_attack_point_t){100,200,50};
    sample.segments[0].end=sample.segments[0].start;
    assert(t3_psx_attack_place(&sample,0,(t3_vec3i_t){1000,0,-4},(t3_vec3i_t){0},
                 (t3_vec3i_t){1000,0,-4},0xC000,0xC000,&trig,NULL,&out));
    assert(out.segments[0].end.x==950 && out.segments[0].end.y==200 && out.segments[0].end.z==96);
    sample.segments[0].start=sample.segments[0].end=(t3_attack_point_t){0};
    assert(t3_psx_attack_place(&sample,0,(t3_vec3i_t){900,0,0},(t3_vec3i_t){0},
                 (t3_vec3i_t){1000,0,0},0,0x4000,&trig,NULL,&out));
    assert(out.segments[0].end.x==1000 && out.segments[0].end.z==-100);
    sample.segments[0].previous_start=1;previous.segment_count=1;
    previous.segments[0].end=(t3_vec3i_t){123,456,789};
    assert(t3_psx_attack_place(&sample,2,(t3_vec3i_t){0},(t3_vec3i_t){0},
                 (t3_vec3i_t){0},0,0,&trig,&previous,&out));
    assert(out.segments[0].start.x==123 && out.segments[0].start.y==456);
    before=out;
    assert(!t3_psx_attack_place(&sample,2,(t3_vec3i_t){0},(t3_vec3i_t){0},
                 (t3_vec3i_t){0},0,0,&trig,NULL,&out));
    assert(!memcmp(&out,&before,sizeof(out)));
    return 0;
}
