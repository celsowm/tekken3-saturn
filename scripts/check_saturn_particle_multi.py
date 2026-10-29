#!/usr/bin/env python3
"""Compare one coherent multi-particle diagnostic to the captured VDP2 data."""
import argparse
import json
from pathlib import Path
import struct
ROOT=Path(__file__).resolve().parents[1]
def div0(n,d):return n//d if n>=0 else -((-n)//d)
def mapx(x):
    n=x*20
    return div0(n+(11 if n>=0 else -11),23)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path)
    p.add_argument('--assets',type=Path,default=ROOT/'assets/imported/tekken3-usa/particles/EFFECTS.BIN')
    p.add_argument('--out',type=Path);a=p.parse_args();root=a.capture
    def u32(i):return struct.unpack('>I',(root/f'symbol-{i}.bin').read_bytes())[0]
    prepared,published,frame,serial,tile_count,particle_count=[u32(i) for i in range(6)]
    if not published or not serial or not 0<=prepared-published<=1 or tile_count>80:raise ValueError('invalid publication metadata')
    draw=(root/'symbol-8.bin').read_bytes()
    count,visible,prepared_frame=struct.unpack_from('>III',draw,2560)
    if frame!=prepared_frame or particle_count!=visible or count>80:raise ValueError('capture stopped between images')
    raw=a.assets.read_bytes()
    if len(raw)!=2948 or raw[:8]!=b'T3FX001\0':raise ValueError('invalid assets')
    tex=[raw[16+i*128:16+(i+1)*128] for i in range(2)]
    palettes={struct.unpack_from('<H',raw,272+68*i)[0]:struct.unpack_from('<16H',raw,276+68*i) for i in range(37)}
    cells={};overlap=0
    for i in range(count):
        at=i*32;xy=struct.unpack_from('>8h',draw,at);shown=draw[at+26]
        if not shown:continue
        clut,slot,pattern=struct.unpack_from('>HBB',draw,at+28)
        if pattern>1 or clut not in palettes:raise ValueError('unknown particle assets')
        left,right=mapx(xy[0]),mapx(xy[2]);top,bottom=div0(xy[1]-20,2),div0(xy[5]-20,2)
        w,h=right-left,bottom-top
        if not(0<w<=32 and 0<h<=32):raise ValueError('unsupported diagnostic sprite')
        x0,y0,x1,y1=max(0,left),max(0,top),min(320,right),min(224,bottom)
        if x0>=x1 or y0>=y1:continue
        for ty in range(y0//8,(y1-1)//8+1):
            for tx in range(x0//8,(x1-1)//8+1):
                cell=cells.setdefault((tx,ty),[0]*64)
                for y in range(8):
                    for x in range(8):
                        px,py=tx*8+x,ty*8+y
                        if not(x0<=px<x1 and y0<=py<y1):continue
                        u,v=(px-left)*16//w,(py-top)*16//h;k=v*16+u;pair=tex[pattern][k//2]
                        color=palettes[clut][(pair&15) if k&1 else pair>>4]
                        if not color:continue
                        old=cell[y*8+x]
                        if old:overlap+=1
                        channels=[min(31,((old>>shift)&31)+((color>>shift)&31)) for shift in (0,5,10)]
                        cell[y*8+x]=0x8000|channels[0]|(channels[1]<<5)|(channels[2]<<10)
    if len(cells)!=tile_count:raise ValueError('tile count mismatch')
    xs=(root/'symbol-6.bin').read_bytes();ys=(root/'symbol-7.bin').read_bytes()
    if list(cells)!=list(zip(xs[:tile_count],ys[:tile_count])):raise ValueError('tile order mismatch')
    ranges=json.loads((root/'ymir.json').read_text())['vdp2']['vram_ranges']
    def get(base,size):
        matches=[r['words'] for r in ranges if r['base_word']==base and len(r['words'])==size]
        if len(matches)!=1:raise ValueError('missing VRAM dump')
        return matches[0]
    names=get(0x30000,8192);actual=get(0x32040,5120);expected_names=[0,0x3200]*4096;expected=[]
    for i,((x,y),cell) in enumerate(cells.items()):
        expected_names[(y*64+x)*2+1]=0x3204+i*4;expected.extend(cell)
    if names!=expected_names or actual[:len(expected)]!=expected:raise ValueError('VRAM content mismatch')
    report=dict(frame=frame,serial=serial,prepared=prepared,published=published,
                visible_particles=visible,tiles=tile_count,overlapping_texel_writes=overlap,
                map_words_checked=8192,cell_words_checked=len(expected),
                scope='Coherent Ymir multi-sprite diagnostic; approximate sampling, no actor occlusion or hardware proof')
    text=json.dumps(report,indent=2)+'\n'
    if a.out:a.out.write_text(text)
    print(text,end='');return 0
if __name__=='__main__':raise SystemExit(main())
