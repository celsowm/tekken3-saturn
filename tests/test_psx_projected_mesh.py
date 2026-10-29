#!/usr/bin/env python3
"""Compare complete C fighter meshes to original DrawSkinnedPart sequences."""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess
import sys
from test_psx_vertex_exchange import ROOT, RESEARCH, KMD, FIGHTER, PRIM, OT, SCR_XY, SCR_Z, set_gte, rotation

def main():
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);a=p.parse_args()
    models=[ROOT/'assets/imported/tekken3-usa/raw/071.bin',ROOT/'assets/imported/law-import/raw/079.bin']
    if not (ROOT/'third_party/Tekken3Remake/work/jp_rev1/exe.bin').is_file() or not all(x.is_file() for x in models):return 77
    sys.path.insert(0,str(RESEARCH))
    try:
        from psxcpu import load_release
        import kmd
    except ImportError:return 77
    rng=random.Random(0x36f00);inputs=[];expected=[];blocks=0
    for iteration in range(5):
        for path in models:
            raw=path.read_bytes();model=kmd.parse(raw);cpu=load_release();cpu.write(KMD,raw);cpu.write(FIGHTER,bytes(0x2000))
            cpu.call(0x80035138,KMD,27,FIGHTER);rows=[];wanted=[]
            for part,row_index in enumerate(kmd.PART_ROW[:18]):
                m=rotation(rng) if iteration else [[4096,0,0],[0,4096,0],[0,0,4096]]
                t=(rng.randrange(-5000,5001),rng.randrange(-3500,3501),rng.choice((0,100,800,4000,32000,70000)))
                for ri in [row_index]+([row_index+1] if part in (1,2,17) else []):
                    vb=model.rows[ri].verts
                    if vb is None:continue
                    set_gte(cpu.gte,m,t);cpu.call(0x80036f00,KMD+0x10+ri*0x38,PRIM,OT,0)
                    n=len(vb.import_scratch)+len(vb.import_global)+len(vb.coords)
                    xy=struct.unpack(f'<{n}I',cpu.read(SCR_XY,4*n));z=struct.unpack(f'<{n}h',cpu.read(SCR_Z,2*n))
                    wanted.extend(zip(xy,z))
                    row=[part,len(vb.import_scratch),len(vb.import_global),len(vb.coords),
                         *[v for r in m for v in r],*t,*vb.import_scratch,*vb.import_global,
                         *[v for xyz in vb.coords for v in xyz]]
                    for group in vb.post:row.extend((len(group),*group))
                    rows.append(row);blocks+=1
            inputs.append(str(len(rows))+'\n'+''.join(' '.join(map(str,row))+'\n' for row in rows))
            expected.append([list(v) for v in wanted])
    run=subprocess.run([str(a.runner.resolve())],input=''.join(inputs),text=True,capture_output=True,check=True)
    actual=[json.loads(line) for line in run.stdout.splitlines()]
    if actual!=expected:
        for i,(got,want) in enumerate(zip(actual,expected)):
            if got!=want:
                for j,(g,w) in enumerate(zip(got,want)):
                    if g!=w:raise AssertionError(f'fighter {i}, slot {j}: {g} != {w}')
                raise AssertionError(f'fighter {i} slot count mismatch')
        raise AssertionError('fighter count mismatch')
    print(json.dumps(dict(fighters=len(expected),blocks=blocks,slots=sum(map(len,expected)),scope='complete Paul/Law block sequences against original DrawSkinnedPart')))
    return 0
if __name__=='__main__':raise SystemExit(main())
