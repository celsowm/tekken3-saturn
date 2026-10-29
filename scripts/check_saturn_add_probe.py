#!/usr/bin/env python3
"""Compare a fixed NBG1 additive test with its matching control screenshot.

This validates the emulator's RGB output for the probe, not PS1 rasterization
or particle occlusion. The two inputs must be the same scene and dimensions.
"""
import argparse
import json
from pathlib import Path
from PIL import Image


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('control',type=Path);p.add_argument('candidate',type=Path)
    p.add_argument('--out',type=Path)
    a=p.parse_args()
    control=Image.open(a.control).convert('RGB');candidate=Image.open(a.candidate).convert('RGB')
    if control.size!=(320,224) or candidate.size!=control.size:
        raise ValueError('expected two 320x224 screenshots')
    bad_inside=bad_outside=0;examples=[]
    for y in range(224):
        for x in range(320):
            before=control.getpixel((x,y));after=candidate.getpixel((x,y))
            inside=80<=x<160 and 56<=y<152
            expected=tuple(min(255,v+d) for v,d in zip(before,(40,56,72))) if inside else before
            if after!=expected:
                if inside:bad_inside+=1
                else:bad_outside+=1
                if len(examples)<10:examples.append(dict(x=x,y=y,before=before,actual=after,expected=expected))
    result=dict(passed=not (bad_inside or bad_outside),inside_pixels=80*96,
                outside_pixels=320*224-80*96,inside_mismatches=bad_inside,
                outside_mismatches=bad_outside,examples=examples,
                scope='One Ymir RGB screenshot pair; not particle rendering or hardware validation')
    text=json.dumps(result,indent=2)+'\n'
    if a.out:a.out.write_text(text)
    print(text,end='')
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
