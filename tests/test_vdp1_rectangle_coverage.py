#!/usr/bin/env python3
"""Compare diagnostic axis-aligned masks with captured Ymir sprite pixels."""
import argparse
import json
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);a=p.parse_args()
    fixture=json.loads((ROOT/'tests/fixtures/vdp1_rectangle_coverage.json').read_text())
    pattern=('11111111','10000001','10111001','10101001',
             '10111001','10000001','10101001','11111111')
    pixels=[int(v) for row in pattern for v in row];cases=[];expected=[]
    for item in fixture['cases']:
        for sample in item['samples']:
            cases.append([*item['rect'],*sample['tile'],0,*pixels]);expected.append(sample['mask'])
    reduced=json.loads((ROOT/'tests/fixtures/vdp1_reduced_uv.json').read_text())
    for item in reduced['cases']:
        left,top,right,bottom=item['rect'];width=right-left+1
        for axis in range(2):
            for target in range(8):
                one_hot=[int((x if axis==0 else y)==target) for y in range(8) for x in range(8)]
                for ty in range(top//8,bottom//8+1):
                    for tx in range(left//8,right//8+1):
                        rows=[]
                        for py in range(ty*8,ty*8+8):
                            row=0
                            for px in range(tx*8,tx*8+8):
                                inside=left<=px<=right and top<=py<=bottom
                                uv=item['uv'][(py-top)*width+(px-left)] if inside else None
                                row=(row<<1)|int(inside and uv[axis]==target)
                            rows.append(row)
                        cases.append([left,top,right,bottom,tx,ty,0,*one_hot]);expected.append(rows)
    run=subprocess.run([str(a.runner.resolve()),'--rect'],input=''.join(' '.join(map(str,x))+'\n' for x in cases),
                       text=True,capture_output=True,check=True)
    actual=[json.loads(x) for x in run.stdout.splitlines()]
    if actual!=expected:
        for i,(g,w) in enumerate(zip(actual,expected)):
            if g!=w:raise AssertionError(f'case {i}: {g} != {w}')
        raise AssertionError('case count mismatch')
    print(json.dumps(dict(expanded_rectangles=len(fixture['cases']),reduced_rectangles=len(reduced['cases']),
                          tiles=len(cases),scope='Ymir authored axis-aligned sprites; no PS1 or hardware claim')))
    return 0
if __name__=='__main__':raise SystemExit(main())
