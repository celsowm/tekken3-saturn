#!/usr/bin/env python3
"""Compare the static original-spark VDP2 probe with a matched control."""
import argparse
import json
from pathlib import Path
import struct
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('control',type=Path);p.add_argument('candidate',type=Path)
    p.add_argument('--assets',type=Path,default=ROOT/'assets/imported/tekken3-usa/particles/EFFECTS.BIN')
    p.add_argument('--out',type=Path);args=p.parse_args()
    raw=args.assets.read_bytes()
    if len(raw)!=2948 or raw[:8]!=b'T3FX001\0':raise ValueError('invalid private particle asset')
    palette_offset=272+25*68
    if struct.unpack_from('<H',raw,palette_offset)[0]!=0x7f94:raise ValueError('missing probe palette')
    palette=struct.unpack_from('<16H',raw,palette_offset+4);pattern=raw[16:144]
    before=Image.open(args.control).convert('RGB');after=Image.open(args.candidate).convert('RGB')
    if before.size!=(320,224) or after.size!=before.size:raise ValueError('expected 320x224 images')
    errors_inside=errors_outside=transparent=black=colored=0;examples=[]
    for y in range(224):
        for x in range(320):
            a=before.getpixel((x,y));b=after.getpixel((x,y))
            inside=80<=x<96 and 64<=y<80
            if inside:
                pixel=(y-64)*16+x-80;pair=pattern[pixel//2]
                index=(pair&15) if pixel&1 else pair>>4
                word=palette[index]
                if word==0:transparent+=1
                elif word==0x8000:black+=1
                else:colored+=1
                add=((word&31)*8,((word>>5)&31)*8,((word>>10)&31)*8) if word else (0,0,0)
                expected=tuple(min(255,a[k]+add[k]) for k in range(3))
            else:expected=a
            if b!=expected:
                if inside:errors_inside+=1
                else:errors_outside+=1
                if len(examples)<10:examples.append(dict(x=x,y=y,before=a,actual=b,expected=expected))
    report=dict(passed=not(errors_inside or errors_outside),inside_pixels=256,outside_pixels=320*224-256,
                transparent=transparent,visible_black=black,colored=colored,
                inside_mismatches=errors_inside,outside_mismatches=errors_outside,examples=examples,
                scope='One static normal-spark texture and CLUT on Ymir; not dynamic rendering or hardware')
    text=json.dumps(report,indent=2)+'\n'
    if args.out:args.out.write_text(text)
    print(text,end='')
    return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
