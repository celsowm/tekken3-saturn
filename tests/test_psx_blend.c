#include "psx_blend.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
static unsigned samples;
static int sample(void* user,uint32_t row,unsigned frame,int16_t root[3],t3_psx_matrix_t slots[18]) {
    (void)user;
    ++samples;if(row==99)return 0;
    root[0]=root[2]=0;root[1]=(int16_t)(row*100+frame*2);
    memset(slots,0,18*sizeof(*slots));
    for(unsigned i=0;i<18;++i)for(unsigned k=0;k<3;++k)slots[i].m[k][k]=4096;
    return 1;
}
int main(void) {
    t3_psx_blend_t s={0};
    t3_psx_blend_facts_t f={0};
    f.row=f.stance_row=s.last_row=1;f.length=30;f.pose_frame=15;f.frame_step=1;
    assert(t3_psx_blend_prepare(&s,&f,sample,0));
    assert(s.mode==T3_BLEND_STANCE && s.active && s.frames==16 && !s.counter);
    assert(s.src_frame==30 && !s.dst_frame && s.root_delta[1]==-60);
    assert(t3_psx_blend_root_y(&s,100)==100 && !s.weight);
    ++f.pose_frame;
    assert(t3_psx_blend_prepare(&s,&f,sample,0));
    assert(!s.active && s.counter==1);
    assert(t3_psx_blend_root_y(&s,100)==97 && s.weight==256);
    f.no_forward=1;
    assert(t3_psx_blend_prepare(&s,&f,sample,0));assert(!s.mode && !s.frames && !s.counter);
    assert(t3_psx_blend_root_y(&s,-100)==-100 && s.weight==4096);

    s=(t3_psx_blend_t){0};f.no_forward=0;f.pose_frame=10;f.pending=1;
    f.pending_row=2;f.entry_frame=20;f.transition=0;
    assert(t3_psx_blend_prepare(&s,&f,sample,0));
    assert(s.mode==T3_BLEND_BRANCH && s.frames==11 && !s.counter);
    assert(s.src_frame==20 && !s.dst_frame && s.root_delta[1]==60);
    s.mode=0;f.transition=0x1A;
    assert(t3_psx_blend_prepare(&s,&f,sample,0));
    assert(s.dst_frame==20 && s.root_delta[1]==100);

    s=(t3_psx_blend_t){0};s.last_row=1;s.previous_root[1]=110;
    f=(t3_psx_blend_facts_t){0};f.row=f.stance_row=2;f.length=30;f.active_first=8;
    f.pose_frame=1;f.changed=1;f.frame_step=1;f.previous_event_frame=7;
    assert(t3_psx_blend_prepare(&s,&f,sample,0));
    assert(s.mode==T3_BLEND_DECAY && s.frames==7 && s.counter==6);
    assert(s.src_frame==6 && !s.dst_frame && s.root_delta[1]==-90);
    assert(t3_psx_blend_root_y(&s,200)==123 && s.weight==3510);
    f.changed=0;
    assert(t3_psx_blend_prepare(&s,&f,sample,0));assert(s.counter==5);
    f.suppressed=1;
    assert(t3_psx_blend_prepare(&s,&f,sample,0));assert(!s.mode);

    /* Alias slots referring to the same row do not start a decay. */
    s=(t3_psx_blend_t){0};s.last_row=f.row;f.suppressed=0;f.changed=1;
    samples=0;assert(t3_psx_blend_prepare(&s,&f,sample,0));assert(!s.mode && !samples);
    f.branch_kind=3;
    assert(t3_psx_blend_prepare(&s,&f,sample,0));assert(s.mode==T3_BLEND_DECAY && samples==1);
    /* Negative root deltas use truncation toward zero, not arithmetic floor. */
    s.counter=1;s.frames=3;s.root_delta[1]=-1;
    assert(t3_psx_blend_root_y(&s,200)==200);
    f.row=99;
    assert(!t3_psx_blend_prepare(&s,&f,sample,0));
    assert(!t3_psx_blend_prepare(0,&f,sample,0));
    /* A round's global hold bypasses both blend selection and sampling. */
    s=(t3_psx_blend_t){.mode=T3_BLEND_STANCE,.frames=10,.counter=5};
    f.global_hold=1;f.decoded_root_dy=17;samples=0;
    assert(t3_psx_blend_prepare(&s,&f,sample,0));
    assert(!samples && !s.mode && !s.frames && !s.counter && s.previous_root[1]==17);
    s.active=1;s.frames=4;s.counter=2;
    assert(t3_psx_blend_prepare(&s,&f,sample,0));
    assert(s.active && s.frames==4 && s.counter==2);
    return 0;
}
