#!/usr/bin/env python3
"""Compare supported parallelogram masks with authored Ymir captures."""
import argparse
import json
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);a=p.parse_args()
    fixture=json.loads((ROOT/'tests/fixtures/vdp1_parallelogram_masks.json').read_text())
    texture=[int(x) for row in ('11111111','10000001','10111001','10101001',
                                '10111001','10000001','10101001','11111111') for x in row]
    cases=[];expected=[];supported=0
    for case in fixture['cases']:
        v=case['vertices'];dx=v[3][0]-v[0][0];dy=v[3][1]-v[0][1];width=v[1][0]-v[0][0];slope=v[1][1]-v[0][1]
        if v[2][0]-v[1][0]!=dx or v[2][1]-v[1][1]!=dy or not 0<=width<=64 or not 0<=slope<=width or not 0<=dy<=64 or abs(dx)>dy:continue
        supported+=1
        for sample in case['samples']:
            cases.append([*[c for point in v for c in point],*sample['tile'],0,*texture]);expected.append(sample['mask'])
    if supported<6:raise AssertionError('missing supported parallelogram fixtures')
    run=subprocess.run([str(a.runner.resolve()),'--parallelogram'],input=''.join(' '.join(map(str,r))+'\n' for r in cases),
                       text=True,capture_output=True,check=True)
    got=[json.loads(line) for line in run.stdout.splitlines()]
    if got!=expected:
        for i,(g,w) in enumerate(zip(got,expected)):
            if g!=w:raise AssertionError(f'tile {i}: {g} != {w}')
        raise AssertionError('tile count mismatch')
    print(json.dumps(dict(supported_quads=supported,recorded=len(fixture['cases']),tiles=len(cases))))
    return 0
if __name__=='__main__':raise SystemExit(main())
