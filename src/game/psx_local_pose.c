#include "psx_local_pose.h"

static void clear(t3_psx_local_pose_cache_t* c) {
    for(unsigned i=0;i<c->capacity;++i)c->entries[i].stream=NULL,c->entries[i].recent=0;
    for(unsigned i=0;i<T3_PSX_LOCAL_POSE_CURSORS;++i)c->cursor[i].stream=NULL,c->cursor_used[i]=0;
    c->cursor_clock=0;
}
/* The cursor of this stream, or the least recently used one. */
static t3_motion_cursor_t* cursor_for(t3_psx_local_pose_cache_t* c,const uint8_t* identity) {
    unsigned best=0;
    for(unsigned i=0;i<T3_PSX_LOCAL_POSE_CURSORS;++i) {
        if(c->cursor[i].stream==identity){best=i;break;}
        if(c->cursor_used[i]<c->cursor_used[best])best=i;
    }
    c->cursor_used[best]=++c->cursor_clock;
    return &c->cursor[best];
}
void t3_psx_local_pose_cache_init(t3_psx_local_pose_cache_t* c,
    t3_psx_local_pose_entry_t* entries,unsigned capacity) {
    unsigned n=entries?capacity:0;
    const int paired=n>=2 && !(n&1u);
    if(!paired && n>255)n=255; /* one set; recent is a byte */
    *c=(t3_psx_local_pose_cache_t){.entries=entries,.capacity=n,
        .sets=paired?n/2:n?1:0,.ways=paired?2:n};
    unsigned span=1;while(span<c->sets)span<<=1;
    c->set_mask=span-1;
    clear(c);
}
int t3_psx_local_pose(const t3_motion_stream_t* s,unsigned frame,
    const t3_motion_tables_t* spline,
    const t3_psx_trig_t* trig,const t3_psx_ik_tables_t* ik,
    t3_psx_local_pose_cache_t* c,int16_t pose[T3_PSX_POSE_CHANNELS],
    t3_psx_matrix_t out[T3_PSX_ROTATION_SLOTS],
    uint32_t* fk_mask,uint32_t* mask,int16_t* root_xx) {
    int16_t sampled[T3_PSX_POSE_CHANNELS];
    t3_psx_matrix_t fk[T3_PSX_ROTATION_SLOTS],result[T3_PSX_ROTATION_SLOTS];
    t3_psx_local_pose_entry_t* set=NULL;
    const uint8_t* identity=NULL;
    uint32_t f=0,m=0;
    if(!s || !s->data || !s->frames || !trig || !ik || !pose || !out ||
       !fk_mask || !mask || !root_xx)return 0;
    if(frame>=s->frames)frame=s->frames-1u;
    if(c && c->capacity) {
        const uint8_t* table=spline?spline->blob:0;
        const size_t length=spline?spline->length:0;
        if(c->fk_trig!=trig->data || c->fk_size!=trig->size ||
           c->ik_trig!=ik->trig.data || c->ik_trig_size!=ik->trig.size ||
           c->ik_blob!=ik->ik_blob || c->ik_size!=ik->ik_size ||
           c->spline!=table || c->spline_size!=length) {
            clear(c);
            c->fk_trig=trig->data;c->fk_size=trig->size;
            c->ik_trig=ik->trig.data;c->ik_trig_size=ik->trig.size;
            c->ik_blob=ik->ik_blob;c->ik_size=ik->ik_size;
            c->spline=table;c->spline_size=length;
        }
        /* The first key's address identifies the opened animation header in
         * its bank, including all-zero channels. */
        identity=s->data+s->key_offset[0];
        const uint32_t hash=(uint32_t)((uintptr_t)identity>>2)*0x9E3779B1u;
        unsigned index=((hash>>16)+frame)&c->set_mask;
        /* Fold the unused tail for non-power-of-two set counts; no SH-2
         * division. Existing power-of-two layouts keep their exact mapping. */
        if(index>=c->sets)index-=c->sets;
        set=&c->entries[index*c->ways];
        for(unsigned w=0;w<c->ways;++w) {
            const t3_psx_local_pose_entry_t* e=&set[w];
            if(e->stream==identity && e->frame==frame && e->frames==s->frames) {
                __builtin_memcpy(pose,e->pose,sizeof(e->pose));
                __builtin_memcpy(out,e->slots,sizeof(e->slots));
                *fk_mask=e->fk_mask;*mask=e->mask;*root_xx=e->root_xx;
                set->recent=(uint8_t)w;
                ++c->hits;return 1;
            }
        }
        ++c->misses;
    }
    if(t3_motion_sample_cursor(s,frame,spline,set?cursor_for(c,identity):NULL,sampled)!=1 ||
       !t3_psx_pose_fk(sampled,trig,fk,&f) ||
       t3_psx_pose_ik(sampled,ik,fk,f,result,&m)!=1)return 0;
    if(set) {
        /* An empty way first, then the way not used last. */
        unsigned w=0;
        while(w<c->ways && set[w].stream)++w;
        if(w==c->ways) {
            w=c->ways==2?1u-set->recent:set->recent+1u;
            if(w>=c->ways)w=0;
            ++c->evictions;
        }
        t3_psx_local_pose_entry_t* e=&set[w];
        __builtin_memcpy(e->pose,sampled,sizeof(sampled));
        __builtin_memcpy(e->slots,result,sizeof(e->slots));
        e->stream=identity;e->frame=(uint8_t)frame;e->frames=s->frames;
        e->fk_mask=f;e->mask=m;e->root_xx=fk[0].m[0][0];
        set->recent=(uint8_t)w;
    }
    __builtin_memcpy(pose,sampled,sizeof(sampled));
    __builtin_memcpy(out,result,sizeof(result));
    *fk_mask=f;*mask=m;*root_xx=fk[0].m[0][0];return 1;
}
