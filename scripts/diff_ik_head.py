#!/usr/bin/env python3
"""Compare src/game/psx_ik.c with a git revision on random inputs, bit for bit.

The tables and poses are random, including values far outside anything a fight
produces, so the original's 32-bit wraps are exercised. It runs on the host, so
it checks the C arithmetic, not the SH-2 build.
"""
import argparse
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx_ik.h"
int old_ik(const int16_t*,const t3_psx_ik_tables_t*,const t3_psx_matrix_t*,uint32_t,t3_psx_matrix_t*,uint32_t*);
static uint64_t state=0x9E3779B97F4A7C15ull;
static uint32_t next(void){state^=state<<13;state^=state>>7;state^=state<<17;return (uint32_t)(state>>16);}
static int16_t value(unsigned kind){
    const uint32_t r=next();
    switch(kind&3u){
        case 0: return (int16_t)r;
        case 1: return (int16_t)((int32_t)(r&0x1FFFu)-0x1000);
        case 2: return (int16_t)((int32_t)(r&0x3FFu)-0x200);
        default: return (int16_t)((r&1u)?0x1000:-0x1000);
    }
}
static void put32(uint8_t* p,int32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
int main(int argc,char** argv){
    static uint8_t trig[T3_PSX_TRIG_BYTES],blob[T3_PSX_IK_BYTES];
    const unsigned long cases=strtoul(argv[argc-1],NULL,10);
    unsigned long results[4]={0},solved=0;
    const t3_psx_ik_tables_t t={{trig,sizeof trig},blob,sizeof blob};
    for(unsigned long c=0;c<cases;++c){
        int16_t pose[T3_PSX_POSE_CHANNELS];
        t3_psx_matrix_t in[T3_PSX_ROTATION_SLOTS],a[T3_PSX_ROTATION_SLOTS],b[T3_PSX_ROTATION_SLOTS];
        uint32_t am=0xAAAAAAAAu,bm=0xAAAAAAAAu;
        const unsigned kind=next();
        if(!(c&255u)){
            for(size_t i=0;i<sizeof trig;++i)trig[i]=(uint8_t)next();
            for(size_t i=0;i<T3_PSX_SQRT_BYTES;i+=2){
                /* Mostly plausible square root mantissas. */
                const uint32_t m=(next()&3u)?(0x4000u+(next()&0x3FFFu)):next();
                blob[i]=(uint8_t)m;blob[i+1]=(uint8_t)(m>>8);
            }
            for(size_t i=0;i<18;i+=2){const int16_t v=value(kind>>2);blob[T3_PSX_SQRT_BYTES+i]=(uint8_t)v;blob[T3_PSX_SQRT_BYTES+i+1]=(uint8_t)(v>>8);}
            for(size_t i=0;i<96;i+=4)put32(blob+T3_PSX_SQRT_BYTES+18+i,(int32_t)(next()&0x3FFu)-0x200);
        }
        for(unsigned i=0;i<T3_PSX_POSE_CHANNELS;++i)pose[i]=value(kind);
        for(unsigned s=0;s<T3_PSX_ROTATION_SLOTS;++s)for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)in[s].m[i][j]=value(kind>>4);
        memset(a,0x55,sizeof a);memset(b,0x55,sizeof b);
        const uint32_t mask=(kind&0x100u)?0x24CCFu:(0x24CCFu|(next()&0x3FFFFu));
        const int ra=old_ik(pose,&t,in,mask,a,&am);
        const int rb=t3_psx_pose_ik(pose,&t,in,mask,b,&bm);
        ++results[ra+1];
        if(ra==1 && (am&~mask))++solved;
        if(ra!=rb || am!=bm || memcmp(a,b,sizeof a)){
            printf("MISMATCH case %lu rc %d %d mask %x %x\n",c,ra,rb,am,bm);
            return 1;
        }
    }
    printf("cases %lu rc-1 %lu rc0 %lu rc1 %lu with limbs solved %lu: identical\n",cases,results[0],results[1],results[2],solved);
    return 0;
}
'''


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--rev', default='HEAD')
    p.add_argument('--cases', type=int, default=2000000)
    p.add_argument('--libsaturn', type=Path, default=Path.home() / 'dev/lib-saturn')
    args = p.parse_args()
    with tempfile.TemporaryDirectory() as folder:
        folder = Path(folder)
        old = folder / 'old_ik.c'
        old.write_bytes(subprocess.check_output(['git', 'show', f'{args.rev}:src/game/psx_ik.c'], cwd=ROOT))
        harness = folder / 'harness.c'
        harness.write_text(HARNESS)
        flags = ['-O2', '-I', str(ROOT / 'src/game'), '-I', str(args.libsaturn / 'include')]
        objects = []
        for source, extra in ((old, ['-Dt3_psx_pose_ik=old_ik', '-Dt3_psx_pose_full=old_full']),
                              (ROOT / 'src/game/psx_ik.c', []), (ROOT / 'src/game/psx_pose.c', []), (harness, [])):
            obj = folder / (source.stem + ('_new' if source.parent != folder else '') + '.o')
            subprocess.run(['cc', *flags, *extra, '-c', str(source), '-o', str(obj)], check=True)
            objects.append(str(obj))
        binary = folder / 'diff'
        subprocess.run(['cc', *objects, '-o', str(binary)], check=True)
        subprocess.run([str(binary), str(args.cases)], check=True)


if __name__ == '__main__':
    main()
