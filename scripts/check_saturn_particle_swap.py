#!/usr/bin/env python3
"""Check the last published test sprite against actual dumped VDP2 VRAM."""
import argparse
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path)
    p.add_argument('--assets',type=Path,default=ROOT/'assets/imported/tekken3-usa/particles/EFFECTS.BIN')
    p.add_argument('--out',type=Path);a=p.parse_args()
    vals=[struct.unpack('>I',(a.capture/f'symbol-{i}.bin').read_bytes())[0] for i in range(6)]
    prepared,published,frame,serial,clut,pattern=vals
    if not published or not serial or not 0<=prepared-published<=1:raise ValueError('invalid publication counters')
    result=json.loads((a.capture/'ymir.json').read_text())
    ranges=[r for r in result['vdp2']['vram_ranges'] if r['base_word']==0x32040 and len(r['words'])==256]
    if len(ranges)!=1:raise ValueError('missing 256-word VDP2 capture')
    raw=a.assets.read_bytes()
    if len(raw)!=2948 or raw[:8]!=b'T3FX001\0':raise ValueError('invalid private particle asset')
    if pattern==255:
        if clut:raise ValueError('blank publication has a palette')
        expected=[0]*256
    else:
        if pattern not in (0,1):raise ValueError('invalid sprite pattern')
        offsets=[272+68*i for i in range(37) if struct.unpack_from('<H',raw,272+68*i)[0]==clut]
        if len(offsets)!=1:raise ValueError('invalid original CLUT')
        palette=struct.unpack_from('<16H',raw,offsets[0]+4);tex=raw[16+pattern*128:16+(pattern+1)*128]
        expected=[]
        for ty in range(2):
            for tx in range(2):
                for y in range(8):
                    for x in range(8):
                        pixel=(ty*8+y)*16+tx*8+x;pair=tex[pixel//2]
                        color=palette[(pair&15) if pixel&1 else pair>>4]
                        expected.append((0x8000|(color&0x7fff)) if color else 0)
    actual=ranges[0]['words']
    if actual!=expected:raise ValueError(f'VDP2 tile mismatch: {sum(x!=y for x,y in zip(actual,expected))} words')
    report=dict(prepared=prepared,published=published,source_frame=frame,swap_serial=serial,clut=clut,
                pattern=pattern,checked_words=256,nonzero_words=sum(x!=0 for x in actual),
                scope='last published stationary diagnostic sprite; Ymir VRAM, not fighter occlusion or hardware')
    text=json.dumps(report,indent=2)+'\n'
    if a.out:a.out.write_text(text)
    print(text,end='');return 0
if __name__=='__main__':raise SystemExit(main())
