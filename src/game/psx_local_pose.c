#include "psx_local_pose.h"

void t3_psx_local_pose_cache_init(t3_psx_local_pose_cache_t* c,
    t3_psx_local_pose_entry_t* entries,unsigned capacity) {
    *c=(t3_psx_local_pose_cache_t){.entries=entries,.capacity=entries?capacity:0};
    for(unsigned i=0;i<c->capacity;++i)entries[i].valid=0;
}
static uint32_t fingerprint(const int16_t* pose) {
    const uint32_t fnv_offset=2166136261u,fnv_prime=16777619u;
    uint32_t hash=fnv_offset;
    for(unsigned i=0;i<T3_PSX_POSE_CHANNELS;++i)
        hash=(hash^(uint16_t)pose[i])*fnv_prime;
    return hash;
}
static int same_pose(const int16_t* a,const int16_t* b) {
    for(unsigned i=0;i<T3_PSX_POSE_CHANNELS;++i)if(a[i]!=b[i])return 0;
    return 1;
}
int t3_psx_local_pose(const int16_t pose[T3_PSX_POSE_CHANNELS],
    const t3_psx_trig_t* trig,const t3_psx_ik_tables_t* ik,
    t3_psx_local_pose_cache_t* c,t3_psx_matrix_t out[T3_PSX_ROTATION_SLOTS],
    uint32_t* fk_mask,uint32_t* mask,int16_t* root_xx) {
    t3_psx_matrix_t fk[T3_PSX_ROTATION_SLOTS],result[T3_PSX_ROTATION_SLOTS];
    uint32_t f=0,m=0,hash=0;
    if(!pose || !trig || !ik || !out || !fk_mask || !mask || !root_xx)return 0;
    if(c && c->capacity) {
        if(c->fk_trig!=trig->data || c->fk_size!=trig->size ||
           c->ik_trig!=ik->trig.data || c->ik_trig_size!=ik->trig.size ||
           c->ik_blob!=ik->ik_blob || c->ik_size!=ik->ik_size) {
            for(unsigned i=0;i<c->capacity;++i)c->entries[i].valid=0;
            c->cursor=0;
            c->fk_trig=trig->data;c->fk_size=trig->size;
            c->ik_trig=ik->trig.data;c->ik_trig_size=ik->trig.size;
            c->ik_blob=ik->ik_blob;c->ik_size=ik->ik_size;
        }
        hash=fingerprint(pose);
        for(unsigned i=0;i<c->capacity;++i) {
            const t3_psx_local_pose_entry_t* e=&c->entries[i];
            if(e->valid && e->fingerprint==hash && same_pose(e->pose,pose)) {
                for(unsigned j=0;j<T3_PSX_ROTATION_SLOTS;++j)out[j]=e->slots[j];
                *fk_mask=e->fk_mask;*mask=e->mask;*root_xx=e->root_xx;
                ++c->hits;return 1;
            }
        }
        ++c->misses;
    }
    if(!t3_psx_pose_fk(pose,trig,fk,&f) ||
       t3_psx_pose_ik(pose,ik,fk,f,result,&m)!=1)return 0;
    if(c && c->capacity) {
        t3_psx_local_pose_entry_t* e=&c->entries[c->cursor];
        if(e->valid)++c->evictions;
        for(unsigned i=0;i<T3_PSX_POSE_CHANNELS;++i)e->pose[i]=pose[i];
        for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i)e->slots[i]=result[i];
        e->fingerprint=hash;e->fk_mask=f;e->mask=m;e->root_xx=fk[0].m[0][0];e->valid=1;
        if(++c->cursor==c->capacity)c->cursor=0;
    }
    for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i)out[i]=result[i];
    *fk_mask=f;*mask=m;*root_xx=fk[0].m[0][0];return 1;
}
