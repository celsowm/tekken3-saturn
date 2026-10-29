#!/usr/bin/env python3
"""Compare bounded quadrilateral masks with authored Ymir captures."""
import argparse
import json
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def supported(v):
    left=v[3][1]-v[0][1];right=v[2][1]-v[1][1]
    return (0<=left<=64 and 0<=right<=64 and
            abs(v[3][0]-v[0][0])<=left and abs(v[2][0]-v[1][0])<=right)
def main():
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);a=p.parse_args()
    fixture=json.loads((ROOT/'tests/fixtures/vdp1_parallelogram_masks.json').read_text())
    texture=[int(x) for row in ('11111111','10000001','10111001','10101001',
                                '10111001','10000001','10101001','11111111') for x in row]
    cases=[];expected=[];n=0
    for item in fixture['cases']:
        if not supported(item['vertices']):continue
        n+=1
        for sample in item['samples']:
            cases.append([*[c for point in item['vertices'] for c in point],*sample['tile'],0,*texture]);expected.append(sample['mask'])
    if n<7:raise AssertionError('missing supported quadrilateral fixtures')
    coordinates=json.loads((ROOT/'tests/fixtures/vdp1_distorted_uv.json').read_text())
    for item in coordinates['cases']:
        if not supported(item['vertices']):continue
        l,t,r,b=item['bounds'];points={(x,y) for x,y,_,_ in item['points']}
        for ty in range(t//8,b//8+1):
            for tx in range(l//8,r//8+1):
                rows=[]
                for y in range(ty*8,ty*8+8):
                    row=0
                    for x in range(tx*8,tx*8+8):row=(row<<1)|int((x,y) in points)
                    rows.append(row)
                cases.append([*[c for point in item['vertices'] for c in point],tx,ty,1,*texture]);expected.append(rows)
    # Each one-hot texture is drawn independently. Unlike an opaque UV
    # image, this preserves earlier writes when later samples are transparent.
    planes=json.loads((ROOT/'tests/fixtures/vdp1_texture_planes.json').read_text())
    if len(planes['cases']) != 16:raise AssertionError('missing U/V planes')
    for item in planes['cases']:
        axis=item['axis'];texel=item['texel']
        tex=[int((x if axis=='u' else y)==texel) for y in range(8) for x in range(8)]
        for sample in item['samples']:
            cases.append([*[c for point in item['vertices'] for c in point],*sample['tile'],0,*tex])
            expected.append(sample['mask'])
    run=subprocess.run([str(a.runner.resolve()),'--quad'],input=''.join(' '.join(map(str,r))+'\n' for r in cases),
                       text=True,capture_output=True,check=True)
    got=[json.loads(line) for line in run.stdout.splitlines()]
    if got!=expected:
        for i,(g,w) in enumerate(zip(got,expected)):
            if g!=w:raise AssertionError(f'tile {i}: {g} != {w}')
        raise AssertionError('tile count mismatch')
    # This previously passed preflight but disagreed with real captured UVs.
    small=json.loads((ROOT/'tests/fixtures/vdp1_small_triangles_uv.json').read_text())
    for rotated in small['cases']:
        if rotated['vertices'][0][1] == rotated['vertices'][1][1]:
            raise AssertionError('fixture is not a rotated triangle')
        left,top,_,_=rotated['bounds']
        data=[*[c for point in rotated['vertices'] for c in point],left//8,top//8,0,*texture]
        rejected=subprocess.run([str(a.runner.resolve()),'--quad'],
                                input=' '.join(map(str,data))+'\n',text=True,capture_output=True)
        if rejected.returncode!=2 or rejected.stdout:
            raise AssertionError(f'rotated triangle must fail preflight: {rotated["vertices"]}')
    print(json.dumps(dict(supported_quads=n,recorded=len(fixture['cases']),tiles=len(cases))))
    return 0
if __name__=='__main__':raise SystemExit(main())
