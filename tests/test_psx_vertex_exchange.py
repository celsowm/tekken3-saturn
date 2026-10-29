#!/usr/bin/env python3
"""Compare C projected seam exchange with original DrawSkinnedPart on real KMDs."""
import argparse
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
RESEARCH=ROOT/'third_party/Tekken3Remake/tools/research'
KMD=0x80150000;FIGHTER=0x801a0000;PRIM=0x80160000;OT=0x801c0000
SCR_XY=0x1f800000;SCR_Z=0x1f8001b8;GLB_XY=0x8009c0f4;GLB_Z=0x8009d0f6

def set_gte(g,m,t):
    pk=lambda a,b:(a&65535)|((b&65535)<<16)
    g.c[0]=pk(m[0][0],m[0][1]);g.c[1]=pk(m[0][2],m[1][0]);g.c[2]=pk(m[1][1],m[1][2])
    g.c[3]=pk(m[2][0],m[2][1]);g.c[4]=m[2][2]&65535
    g.c[5:8]=[v&0xffffffff for v in t];g.c[24]=192<<16;g.c[25]=240<<16;g.c[26]=500

def rotation(rng):
    a,b,c=(rng.uniform(-0.8,0.8) for _ in range(3))
    ca,sa,cb,sb,cc,sc=math.cos(a),math.sin(a),math.cos(b),math.sin(b),math.cos(c),math.sin(c)
    rows=((cb*cc,sb*sa*cc-ca*sc,sa*sc+sb*ca*cc),
          (cb*sc,ca*cc+sb*sa*sc,ca*sc*sb-sa*cc),(-sb,sa*cb,ca*cb))
    return [[round(v*4096) for v in row] for row in rows]

def main():
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);a=p.parse_args()
    exe=ROOT/'third_party/Tekken3Remake/work/jp_rev1/exe.bin'
    models=[ROOT/'assets/imported/tekken3-usa/raw/071.bin',ROOT/'assets/imported/law-import/raw/079.bin']
    if not exe.is_file() or not all(f.is_file() for f in models):return 77
    sys.path.insert(0,str(RESEARCH))
    try:
        from psxcpu import load_release,GTE
        import kmd
    except ImportError:return 77
    rng=random.Random(0x36f00);rows=[];project_rows=[];expected=[];touched=set();groups=[0]*6
    for iteration in range(5):
        for model_path in models:
            raw=model_path.read_bytes();model=kmd.parse(raw);cpu=load_release();cpu.write(KMD,raw);cpu.write(FIGHTER,bytes(0x2000))
            cpu.call(0x80035138,KMD,27,FIGHTER);touched=set();first=True
            for part,row_index in enumerate(kmd.PART_ROW[:18]):
                indices=[row_index]+([row_index+1] if part in (1,2,17) else [])
                for ri in indices:
                    row=model.rows[ri];vb=row.verts
                    if vb is None:continue
                    # Varied fixed-point transforms include negative screen x/y and
                    # GTE depth saturation, while the original computes all seams.
                    m=rotation(rng) if iteration else [[4096,0,0],[0,4096,0],[0,0,4096]]
                    t=(rng.randrange(-5000,5001),rng.randrange(-3500,3501),rng.choice((0,100,800,4000,32000,70000)))
                    set_gte(cpu.gte,m,t);g=GTE();set_gte(g,m,t)
                    own=[]
                    for x,y,z in vb.coords:
                        g.d[0]=(x&65535)|((y&65535)<<16);g.d[1]=z&65535;g.command(0x00080001)
                        own.append((g.d[14],(g.d[19]&65535)>>5))
                    cpu.call(0x80036f00,KMD+0x10+ri*0x38,PRIM,OT,0)
                    n=len(vb.import_scratch)+len(vb.import_global)+len(vb.coords)
                    game_xy=struct.unpack(f'<{n}I',cpu.read(SCR_XY,4*n));game_z=struct.unpack(f'<{n}h',cpu.read(SCR_Z,2*n))
                    for gi in (0,3):touched.update((e&255)//2 for e in vb.post[gi])
                    game_global={i:(struct.unpack('<I',cpu.read(GLB_XY+4*i,4))[0],
                                    struct.unpack('<h',cpu.read(GLB_Z+2*i,2))[0]) for i in touched}
                    header=[int(first),len(vb.import_scratch),len(vb.import_global),len(own)]
                    tokens=[*header,*vb.import_scratch,*vb.import_global]
                    tokens.extend(v for pair in own for v in pair)
                    projected=[*header,*[v for row in m for v in row],*t,500,*vb.import_scratch,*vb.import_global]
                    projected.extend(v for point in vb.coords for v in point)
                    for gi,lst in enumerate(vb.post):
                        tokens.extend((len(lst),*lst));projected.extend((len(lst),*lst));groups[gi]+=len(lst)
                    rows.append(tokens);project_rows.append(projected)
                    expected.append((list(zip(game_xy,game_z)),game_global));first=False
    run=subprocess.run([str(a.runner.resolve())],input=''.join(' '.join(map(str,r))+'\n' for r in rows),
                       text=True,capture_output=True,check=True)
    actual=[json.loads(line) for line in run.stdout.splitlines()]
    if len(actual)!=len(expected):raise AssertionError('part count mismatch')
    comparisons=0
    for part,(want,got) in enumerate(zip(expected,actual)):
        scratch,glob=want
        if [tuple(x) for x in got['scratch']]!=scratch:raise AssertionError(f'scratch mismatch part {part}')
        for i,value in glob.items():
            if tuple(got['global'][i])!=value:raise AssertionError(f'global mismatch part {part} index {i}')
        comparisons+=len(scratch)+len(glob)
    project_run=subprocess.run([str(a.runner.resolve()),'--project'],
        input=''.join(' '.join(map(str,r))+'\n' for r in project_rows),text=True,capture_output=True,check=True)
    projected=[json.loads(line) for line in project_run.stdout.splitlines()]
    if projected!=actual:
        for i,(want,got) in enumerate(zip(actual,projected)):
            if want!=got:raise AssertionError(f'projection plus seam differs from original part {i}: {want["scratch"][:3]} vs {got["scratch"][:3]}')
        raise AssertionError('projection part count mismatch')
    mesh_run=subprocess.run([str(a.runner.resolve()),'--mesh'],
        input=''.join(' '.join(map(str,r))+'\n' for r in project_rows),text=True,capture_output=True,check=True)
    if [json.loads(line) for line in mesh_run.stdout.splitlines()] != actual:
        raise AssertionError('projected mesh output differs from original')
    print(json.dumps(dict(parts=len(rows),slot_comparisons=comparisons,post_group_counts=groups,
                          scope='Original DrawSkinnedPart vs C projection and exchange, private Paul/Law, five transform sequences')))
    return 0
if __name__=='__main__':raise SystemExit(main())
