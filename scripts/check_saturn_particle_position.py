#!/usr/bin/env python3
"""Check the positioned diagnostic particle's map and RGB cells in Ymir VRAM."""
import argparse
import json
from pathlib import Path
import struct

ROOT=Path(__file__).resolve().parents[1]
def truncdiv(n,d):return n//d if n>=0 else -((-n)//d)
def mx(x):
    n=x*20
    return truncdiv(n+(11 if n>=0 else -11),23)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path)
    p.add_argument('--assets',type=Path,default=ROOT/'assets/imported/tekken3-usa/particles/EFFECTS.BIN')
    p.add_argument('--out',type=Path);args=p.parse_args();root=args.capture
    def word(i):return struct.unpack('>I',(root/f'symbol-{i}.bin').read_bytes())[0]
    prepared,published,frame,serial,clut,pattern,count=[word(i) for i in range(7)]
    if not published or not serial or not 0<=prepared-published<=1 or not 1<=count<=25:raise ValueError('invalid active publication')
    xs=(root/'symbol-7.bin').read_bytes();ys=(root/'symbol-8.bin').read_bytes()
    q=struct.unpack_from('>8h4HhB',(root/'symbol-9.bin').read_bytes());xy=q[:8]
    if not q[-1] or xy[1]!=xy[3] or xy[5]!=xy[7] or xy[0]!=xy[4] or xy[2]!=xy[6]:raise ValueError('invalid quad')
    left,right=mx(xy[0]),mx(xy[2]);top,bottom=truncdiv(xy[1]-20,2),truncdiv(xy[5]-20,2)
    w,h=right-left,bottom-top
    if not(0<w<=32 and 0<h<=32):raise ValueError('unsupported diagnostic size')
    x0,y0,x1,y1=max(0,left),max(0,top),min(320,right),min(224,bottom)
    tiles=[(x,y) for y in range(y0//8,(y1-1)//8+1) for x in range(x0//8,(x1-1)//8+1)]
    if len(tiles)!=count or tiles!=list(zip(xs[:count],ys[:count])):raise ValueError('tile coordinates differ')
    raw=args.assets.read_bytes()
    if len(raw)!=2948 or raw[:8]!=b'T3FX001\0' or pattern not in (0,1):raise ValueError('invalid asset or pattern')
    offsets=[272+68*i for i in range(37) if struct.unpack_from('<H',raw,272+68*i)[0]==clut]
    if len(offsets)!=1:raise ValueError('invalid CLUT')
    pal=struct.unpack_from('<16H',raw,offsets[0]+4);tex=raw[16+pattern*128:16+(pattern+1)*128]
    vram=json.loads((root/'ymir.json').read_text())['vdp2']['vram_ranges']
    def vram_range(addr,length):
        ranges=[r['words'] for r in vram if r['base_word']==addr and len(r['words'])==length]
        if len(ranges)!=1:raise ValueError('missing VDP2 dump')
        return ranges[0]
    names=vram_range(0x30000,8192);actual=vram_range(0x32040,1600)
    expected_names=[0,0x3200]*4096;expected_cells=[]
    for i,(tx,ty) in enumerate(tiles):
        expected_names[(ty*64+tx)*2+1]=0x3204+i*4
        for y in range(ty*8,ty*8+8):
            for x in range(tx*8,tx*8+8):
                color=0
                if x0<=x<x1 and y0<=y<y1:
                    u=(x-left)*16//w;v=(y-top)*16//h;k=v*16+u;byte=tex[k//2]
                    color=pal[(byte&15) if k&1 else byte>>4]
                expected_cells.append((0x8000|(color&0x7fff)) if color else 0)
    if names!=expected_names:raise ValueError('VDP2 map mismatch')
    if actual[:count*64]!=expected_cells:raise ValueError('VDP2 texel mismatch')
    report=dict(prepared=prepared,published=published,frame=frame,serial=serial,clut=clut,
                rect=[left,top,right,bottom],tile_count=count,nonzero_texels=sum(bool(x) for x in expected_cells),
                map_words_checked=8192,texture_words_checked=len(expected_cells),
                scope='one positioned sprite in Ymir, diagnostic viewport/nearest sampling; no PS1 pixel or occlusion proof')
    text=json.dumps(report,indent=2)+'\n'
    if args.out:args.out.write_text(text)
    print(text,end='');return 0
if __name__=='__main__':raise SystemExit(main())
