#!/usr/bin/env python3
"""Compare one coherent Saturn particle snapshot with the host evaluator."""
import argparse
import json
from pathlib import Path
import struct
import subprocess

ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('capture',type=Path);p.add_argument('--runner',type=Path,required=True)
    p.add_argument('--exe',type=Path,default=ROOT/'third_party/Tekken3Remake/work/jp_rev1/exe.bin')
    p.add_argument('--assets',type=Path,default=ROOT/'assets/imported/tekken3-usa/particles/EFFECTS.BIN')
    p.add_argument('--out',type=Path);args=p.parse_args()
    a=(args.capture/'symbol-0.bin').read_bytes();b=(args.capture/'symbol-1.bin').read_bytes()
    if len(a)!=1636 or len(b)!=2572:raise ValueError('unexpected native capture ABI')
    n=struct.unpack_from('>I',a,1600)[0];camera=struct.unpack_from('>7i',a,1604)
    frame=struct.unpack_from('>I',a,1632)[0];count,visible,pframe=struct.unpack_from('>III',b,2560)
    if n>80 or n!=count or frame!=pframe:raise ValueError('capture stopped between two images')
    rows=[];native=[]
    for i in range(n):
        x,y,z,c0,c1,slot,flags,model=struct.unpack_from('>iiiHHBBB',a,i*20)
        rows.append([*camera,x,y,z,flags,model,c0,c1,frame&1])
        xy=struct.unpack_from('>8h',b,i*32)
        bucket,shown=struct.unpack_from('>hB',b,i*32+24)
        clut,draw_slot,pattern=struct.unpack_from('>HBB',b,i*32+28)
        if draw_slot!=slot:raise ValueError('native slot mismatch')
        native.append([*xy,bucket,shown,clut,pattern])
    result=subprocess.run([str(args.runner.resolve()),str(args.exe.resolve()),str(args.assets.resolve())],
        input=''.join(' '.join(map(str,row))+'\n' for row in rows),text=True,capture_output=True,check=True)
    host=[json.loads(line) for line in result.stdout.splitlines()]
    if host!=native or sum(row[9] for row in native)!=visible:raise ValueError('SH-2 and host projections differ')
    report=dict(matched_particles=n,source_frame=frame,visible=visible,fields_per_particle=12,
        scope='Same captured snapshot: SH-2 vs host; not PS1 pixels or native drawing')
    text=json.dumps(report,indent=2)+'\n'
    if args.out:args.out.write_text(text)
    print(text,end='')
    return 0
if __name__=='__main__':raise SystemExit(main())
