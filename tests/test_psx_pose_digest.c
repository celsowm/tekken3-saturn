/* Digest of t3_psx_compose_parts over synthetic part tables, slot sets and
 * roots: the W5.4 bit-exact A/B for the immutable-topology fast path. Build it
 * twice, with and without -DT3_POSE_TOPOLOGY_FAST, and compare the printed
 * digest; invalid tables must keep failing (return 0) either way. Host-only. */
#include "psx_pose.h"
#include <stdio.h>
#include <string.h>

static unsigned long long g_hash=1469598103934665603ull;
static unsigned long long g_ok,g_fail;

static void mix(int32_t v) {
    uint32_t u=(uint32_t)v;
    for(int i=0;i<4;++i) {
        g_hash^=(unsigned char)(u>>(8*i));
        g_hash*=1099511628211ull;
    }
}

static int32_t rng=12345;
static int32_t next(void) {
    rng=rng*1103515245+12345;
    return (rng>>8)&0x7FFF;
}

static void fill_matrix(t3_psx_matrix_t* m) {
    for(int r=0;r<3;++r)for(int c=0;c<3;++c)
        m->m[r][c]=(int16_t)((int)next()%4097-2048);
}

/* kind 0: chain; 1: tree; 2: invalid slot; 3: invalid offset; 4: parent loop. */
static void fill_parts(t3_psx_part_t* parts,int kind) {
    for(int i=0;i<T3_PSX_DRAW_PARTS;++i) {
        memset(&parts[i],0,sizeof parts[i]);
        if(kind==4) parts[i].parent=(int8_t)(i+1);
        else if(kind==1) parts[i].parent=(int8_t)(i?i/2:-1);
        else parts[i].parent=(int8_t)(i-1);
        parts[i].slot=(uint8_t)((i*7)%T3_PSX_ROTATION_SLOTS);
        parts[i].offset.x=next()%4000-2000;
        parts[i].offset.y=next()%4000-2000;
        parts[i].offset.z=next()%4000-2000;
    }
    if(kind==2) parts[5].slot=25u;
    if(kind==3) {parts[9].offset.x=0x7FFFFFFF;parts[9].offset.y=0;}
}

int main(void) {
    static t3_psx_part_t parts[5][T3_PSX_DRAW_PARTS];
    static t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS];
    t3_psx_joint_t root,out[T3_PSX_DRAW_PARTS];
    uint32_t masks[3]={0x3FFFFu,0x2AAAAu,0u};
    for(int kind=0;kind<5;++kind)fill_parts(parts[kind],kind);
    for(int want_valid=0;want_valid<2;++want_valid) {
        memset(&root,0,sizeof root);
        fill_matrix(&root.rotation);
        root.origin.x=(want_valid?100:0x7FFFFFFF);
        root.origin.y=(want_valid?-200:0);
        root.origin.z=(want_valid?300:0);
        for(unsigned k=0;k<3;++k) {
            for(int i=0;i<T3_PSX_ROTATION_SLOTS;++i)fill_matrix(&slots[i]);
            for(int kind=0;kind<5;++kind) {
                /* Twice per table: the second call exercises the cached verdict. */
                for(int repeat=0;repeat<2;++repeat) {
                    memset(out,0x5A,sizeof out);
                    uint32_t mask=0;
                    const int ok=t3_psx_compose_parts(parts[kind],slots,masks[k],&root,out,&mask);
                    mix(ok);mix((int32_t)mask);
                    if(ok) {
                        ++g_ok;
                        for(int i=0;i<T3_PSX_DRAW_PARTS;++i) {
                            for(int r=0;r<3;++r)for(int c=0;c<3;++c)mix(out[i].rotation.m[r][c]);
                            mix(out[i].origin.x);mix(out[i].origin.y);mix(out[i].origin.z);
                        }
                    } else {
                        ++g_fail;
                        /* A failed call must commit nothing: digest the buffer. */
                        for(int i=0;i<T3_PSX_DRAW_PARTS;++i) {
                            mix(out[i].origin.x);mix(out[i].origin.y);mix(out[i].origin.z);
                        }
                    }
                }
            }
        }
    }
    printf("ok %llu fail %llu digest %016llx\n",g_ok,g_fail,g_hash);
    return 0;
}
