#!/usr/bin/env python3
"""Compare particle OT traversal with unmodified original EffectsUpdate."""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
RESEARCH=ROOT/'third_party/Tekken3Remake/tools/research'
MODEL=0x80180000
HEADER=0x80181000
OBJECT=0x80182000
OT=0x80190000
SENTINEL=0x800A0D30

def main():
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);a=p.parse_args()
    exe=ROOT/'third_party/Tekken3Remake/work/jp_rev1/exe.bin'
    if not exe.is_file():return 77
    sys.path.insert(0,str(RESEARCH))
    try:
        from psxcpu import load_release
        from unicorn import UC_HOOK_CODE
        from unicorn.mips_const import UC_MIPS_REG_A0,UC_MIPS_REG_A1
    except ImportError:return 77
    cpu=load_release();observed={}
    def word(address):return struct.unpack('<I',cpu.read(address,4))[0]
    def w(address,value):cpu.write(address,struct.pack('<I',value&0xffffffff))
    def hook(uc,address,size,data):
        primitive=uc.reg_read(UC_MIPS_REG_A1);obj=primitive-0x50
        i=(obj-OBJECT)//0xa0
        z=word(uc.reg_read(UC_MIPS_REG_A0))
        flags=cpu.read(obj+13,1)[0]
        observed[i]=((z>>21)&1023)-(5 if flags&0x20 else 0)
    cpu.uc.hook_add(UC_HOOK_CODE,hook,begin=0x8006E734,end=0x8006E734)
    w(0x800A0D38,0x101);w(0x800AE16C,0);w(0x800A911C,HEADER);w(HEADER+4,OT);w(0x800AE3C4,0)
    cpu.write(MODEL,exe.read_bytes()[0x800+0x1554c:0x800+0x1554c+32])
    cpu.write(0x800A8A88,struct.pack('<3i',0,0,0));cpu.write(0x800A8A9C,struct.pack('<3i',0,0,0))
    cpu.gte.write_ctrl(24,192<<16);cpu.gte.write_ctrl(25,240<<16);cpu.gte.write_ctrl(26,500)
    rng=random.Random(0x6e734);inputs=[];expected=[];linked_count=ties=0
    for case in range(80):
        n=80 if case==0 else rng.randrange(1,50)
        w(SENTINEL+4,OBJECT);cpu.write(OT,b'\xff\xff\xff\x00'*1024);observed.clear()
        for i in range(n):
            obj=bytearray(0xa0)
            struct.pack_into('<3I',obj,0,SENTINEL if i==0 else OBJECT+(i-1)*0xa0,
                             SENTINEL if i+1==n else OBJECT+(i+1)*0xa0,MODEL)
            obj[12:15]=bytes([1,rng.choice((0xc0,0xe0)),1])
            # Small set of depths creates deliberate equal-bucket ties; x also
            # generates clipped quads. The original computes actual insertion.
            struct.pack_into('<3i',obj,0x28,rng.choice((0,100,-100,30000)), -100,
                             rng.choice((100,160,1000,1024,2048,4096,16000)))
            cpu.write(OBJECT+i*0xa0,obj)
        cpu.call(0x8006F8A8,0)
        if len(observed)!=n:raise AssertionError(f'missing original projections {case}')
        traversal=[]
        for bucket in range(1023,-1,-1):
            ptr=word(OT+4*bucket)&0xffffff;chain=[]
            while ptr!=0xffffff:
                delta=ptr-((OBJECT+0x50)&0xffffff)
                if delta<0 or delta%0xa0 or delta//0xa0>=n:raise AssertionError('invalid primitive link')
                i=delta//0xa0
                if i in traversal:raise AssertionError('cyclic or duplicate primitive')
                chain.append(i);traversal.append(i)
                ptr=word(OBJECT+i*0xa0+0x50)&0xffffff
            if len(chain)>1:ties+=1
        linked=set(traversal);linked_count+=len(linked)
        inputs.append([n,*[v for i in range(n) for v in (observed[i],int(i in linked))]])
        expected.append(traversal)
    result=subprocess.run([str(a.runner.resolve())],input=''.join(' '.join(map(str,r))+'\n' for r in inputs),
                          text=True,capture_output=True,check=True)
    actual=[json.loads(line) for line in result.stdout.splitlines()]
    if actual!=expected:raise AssertionError(next((i,x,y) for i,(x,y) in enumerate(zip(expected,actual)) if x!=y))
    print(json.dumps(dict(cases=len(inputs),linked_original_primitives=linked_count,equal_bucket_chains=ties,
                          scope='particle-only original OT traversal; actor merge and pixel coverage unverified')))
    return 0
if __name__=='__main__':raise SystemExit(main())
