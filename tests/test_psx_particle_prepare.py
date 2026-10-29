#!/usr/bin/env python3
"""Check combined asset/model/CLUT preparation against captured retail draw calls."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);a=p.parse_args()
    trace=ROOT/'build/parity-particle-projection-original.json'
    exe=ROOT/'third_party/Tekken3Remake/work/jp_rev1/exe.bin'
    asset=ROOT/'assets/imported/tekken3-usa/particles/EFFECTS.BIN'
    if not all(x.is_file() for x in (trace,exe,asset)):return 77
    data=json.loads(trace.read_text()); snapshots={s['frame']:s for s in data['snapshots']}
    rows=[];expected=[]
    for draw in data['projections']:
        frame=draw['frame'];buffer=draw['display_buffer']
        obj=next(o for o in snapshots[frame]['objects'] if o['address']==draw['object'])
        c=obj['canonical']; camera=draw['camera']; h=draw['gte_control'][26]&65535
        if buffer!=frame%2 or c[9:12]!=draw['position'] or c[1]!=(draw['flags_word']>>8)&255:
            raise AssertionError('captured draw and post-update state do not agree')
        rows.append([*camera[:3],*camera[5:8],h,*draw['position'],c[1],c[3],c[7],c[8],buffer])
        uv=tuple(draw['primitive_uv'])
        if uv not in ((128,144),(128,128)):raise AssertionError(f'unsupported captured UV {uv}')
        expected.append([*[v for point in draw['xy'] for v in point],draw['bucket'],int(draw['cull_predicate']),draw['primitive_clut'],int(uv==(128,128))])
    run=subprocess.run([str(a.runner),str(exe),str(asset)],input=''.join(' '.join(map(str,row))+'\n' for row in rows),
                       text=True,capture_output=True,check=True)
    actual=[json.loads(line) for line in run.stdout.splitlines()]
    if len(actual)!=len(expected):raise AssertionError('runner returned an incomplete sprite list')
    if actual!=expected:raise AssertionError(next((i,e,b) for i,(e,b) in enumerate(zip(expected,actual)) if e!=b))
    print(f'{len(actual)} captured retail sprites: coordinates, OT bucket, culling, selected CLUT and pattern match')
    return 0
if __name__=='__main__':raise SystemExit(main())
