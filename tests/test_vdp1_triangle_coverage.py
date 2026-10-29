#!/usr/bin/env python3
"""Compare supported triangle texel masks with authored Ymir captures."""
import argparse
import json
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);a=p.parse_args()
    fixture=json.loads((ROOT/'tests/fixtures/vdp1_triangle_uv.json').read_text())
    cases=[];expected=[];supported=0
    for item in fixture['cases']:
        v=item['vertices'];height=v[2][1]-v[0][1]
        if v[0][1]!=v[1][1] or v[2]!=v[3] or v[0][0]>v[1][0] or height<0:continue
        if any(abs(v[2][0]-v[i][0])>height for i in (0,1)):continue
        supported+=1;left,top,right,bottom=item['bounds'];observed={(x,y):(u,vv) for x,y,u,vv in item['points']}
        for axis in range(2):
            for target in range(8):
                texture=[int((x if axis==0 else y)==target) for y in range(8) for x in range(8)]
                for ty in range(top//8,bottom//8+1):
                    for tx in range(left//8,right//8+1):
                        mask=[]
                        for py in range(ty*8,ty*8+8):
                            row=0
                            for px in range(tx*8,tx*8+8):
                                uv=observed.get((px,py));row=(row<<1)|int(uv is not None and uv[axis]==target)
                            mask.append(row)
                        cases.append([*[c for point in v for c in point],tx,ty,0,*texture]);expected.append(mask)
    if supported<4:raise AssertionError('missing supported triangle fixtures')
    run=subprocess.run([str(a.runner.resolve()),'--triangle'],input=''.join(' '.join(map(str,r))+'\n' for r in cases),
                       text=True,capture_output=True,check=True)
    got=[json.loads(line) for line in run.stdout.splitlines()]
    if got!=expected:
        for i,(g,w) in enumerate(zip(got,expected)):
            if g!=w:raise AssertionError(f'tile {i}: {g} != {w}')
        raise AssertionError('tile count mismatch')
    print(json.dumps(dict(supported_triangles=supported,recorded=len(fixture['cases']),tile_tests=len(cases))))
    return 0
if __name__=='__main__':raise SystemExit(main())
