#include "psx_world_pose.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static t3_psx_world_pose_entry_t entries[128];
static t3_psx_part_t parts[2][T3_PSX_DRAW_PARTS];
static t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS];
static t3_psx_joint_t root;
static t3_psx_matrix_t identity(void) {
    const t3_psx_matrix_t m={{{4096,0,0},{0,4096,0},{0,0,4096}}};return m;
}
static void setup(void) {
    memset(parts,0,sizeof(parts));
    for(unsigned table=0;table<2;++table)for(unsigned i=0;i<T3_PSX_DRAW_PARTS;++i) {
        parts[table][i].slot=(uint8_t)i;
        parts[table][i].parent=(int8_t)(i?(int)((i-1)/2):-1);
        parts[table][i].offset=(t3_vec3i_t){(int)i*7-60,(int)i*3+4,(int)table*9-3};
    }
    for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i)slots[i]=identity();
    root=(t3_psx_joint_t){.rotation=identity()};
}
static int compare(t3_psx_world_pose_cache_t* cache,const t3_psx_part_t* table,uint32_t mask) {
    t3_psx_joint_t expected[T3_PSX_DRAW_PARTS],actual[T3_PSX_DRAW_PARTS];
    memset(expected,0xA5,sizeof(expected));memcpy(actual,expected,sizeof(actual));
    uint32_t a=0xBAD,b=a;
    int x=t3_psx_compose_parts(table,slots,mask,&root,expected,&a);
    int y=t3_psx_compose_cached(table,slots,mask,&root,cache,actual,&b);
    assert(x==y && a==b && !memcmp(expected,actual,sizeof(actual)));
    return x;
}
static void translation_and_bounds(void) {
    t3_psx_world_pose_cache_t cache;
    setup();t3_psx_world_pose_cache_init(&cache,entries,128);
    assert(compare(&cache,parts[0],0x3FFFF));assert(cache.misses==1 && !cache.hits);
    root.origin=(t3_vec3i_t){900,-100,55};
    assert(compare(&cache,parts[0],0x3FFFF));assert(cache.hits==1);
    /* The original bounds every intermediate parent, not just leaves. */
    root.origin=(t3_vec3i_t){T3_PSX_UNIT_LIMIT,T3_PSX_UNIT_LIMIT,T3_PSX_UNIT_LIMIT};
    assert(!compare(&cache,parts[0],0x3FFFF));
    root.origin=(t3_vec3i_t){-T3_PSX_UNIT_LIMIT,-T3_PSX_UNIT_LIMIT,-T3_PSX_UNIT_LIMIT};
    assert(!compare(&cache,parts[0],0x3FFFF));
    root.origin=(t3_vec3i_t){T3_PSX_UNIT_LIMIT+1,0,0};
    assert(!compare(&cache,parts[0],0x3FFFF));
    root.origin=(t3_vec3i_t){0};
    assert(compare(&cache,parts[0],0x3FFFF));
    slots[2].m[1][2]=-1; /* post-tracking local changes cannot hit stale output */
    assert(compare(&cache,parts[0],0x3FFFF));assert(cache.misses==2);
    root.rotation.m[0][1]=1;
    assert(compare(&cache,parts[0],0x3FFFF));assert(cache.misses==3);
    assert(compare(&cache,parts[1],0x3FFFF));assert(cache.misses==4);
    assert(!compare(&cache,parts[0],1u<<19));
    assert(compare(&cache,parts[0],1));
    unsigned hits=cache.hits;
    slots[3].m[0][0]=1234; /* invalid locals are not part of the operation */
    assert(compare(&cache,parts[0],1));assert(cache.hits==hits+1);
    assert(compare(&cache,parts[0],0));
    assert(compare(NULL,parts[0],0x3FFFF));
    t3_psx_world_pose_cache_init(&cache,entries,3);assert(!cache.capacity);
    assert(compare(&cache,parts[0],0x3FFFF));
}
static void collisions_eviction_and_failure(void) {
    t3_psx_world_pose_cache_t cache;
    setup();t3_psx_world_pose_cache_init(&cache,entries,4);
    assert(compare(&cache,parts[0],0x3FFFF));
    t3_psx_world_pose_entry_t saved={0};
    for(unsigned i=0;i<4;++i)if(entries[i].valid)saved=entries[i];
    assert(saved.valid);
    for(unsigned i=0;i<4;++i) {
        entries[i]=saved;entries[i].root.m[0][0]=4095;
        for(unsigned p=0;p<T3_PSX_DRAW_PARTS;++p)entries[i].relative[p].origin.x=999999;
    }
    /* Deliberately matching fingerprints do not bypass root/local keys. */
    assert(compare(&cache,parts[0],0x3FFFF));assert(cache.misses==2);
    for(unsigned i=0;i<20;++i) {
        slots[2].m[1][2]=(int16_t)i;
        assert(compare(&cache,parts[0],0x3FFFF));
    }
    assert(cache.evictions>0);
    /* Returned output cannot mutate the stored result. */
    t3_psx_joint_t out[T3_PSX_DRAW_PARTS];uint32_t mask;
    assert(t3_psx_compose_cached(parts[0],slots,0x3FFFF,&root,&cache,out,&mask));
    memset(out,0,sizeof(out));assert(compare(&cache,parts[0],0x3FFFF));
    /* Reloading table bytes at the same address requires explicit reset. */
    parts[0][3].parent=3;t3_psx_world_pose_cache_init(&cache,entries,128);
    assert(!compare(&cache,parts[0],0x3FFFF));assert(!cache.hits);
    parts[0][3].parent=0;parts[0][4].offset.x=T3_PSX_UNIT_LIMIT+1;
    assert(!compare(&cache,parts[0],0x3FFFF));
    t3_psx_world_pose_cache_init(&cache,NULL,128);assert(!cache.capacity);
}
static uint32_t state=123;
static uint32_t random_word(void){state=state*1664525u+1013904223u;return state;}
static void generated(void) {
    t3_psx_world_pose_cache_t cache;
    setup();t3_psx_world_pose_cache_init(&cache,entries,128);
    for(unsigned trial=0;trial<5000;++trial) {
        for(unsigned j=0;j<T3_PSX_ROTATION_SLOTS;++j)
            for(unsigned r=0;r<3;++r)for(unsigned col=0;col<3;++col)
                slots[j].m[r][col]=(int16_t)(uint16_t)random_word();
        for(unsigned r=0;r<3;++r)for(unsigned col=0;col<3;++col)
            root.rotation.m[r][col]=(int16_t)(uint16_t)random_word();
        const unsigned table=trial%2;const uint32_t mask=random_word()&0x3FFFF;
        root.origin=(t3_vec3i_t){0};
        (void)compare(&cache,parts[table],mask);
        for(unsigned shift=0;shift<4;++shift) {
            root.origin=(t3_vec3i_t){(int)(random_word()%2000001)-1000000,
                (int)(random_word()%2000001)-1000000,(int)(random_word()%2000001)-1000000};
            (void)compare(&cache,parts[table],mask);
        }
    }
    assert(cache.hits>0 && cache.evictions>0);
}
int main(void) {
    translation_and_bounds();collisions_eviction_and_failure();generated();
    puts("Post-blend composition cache matches original results and atomic failures");
    return 0;
}
