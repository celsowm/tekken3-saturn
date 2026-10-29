#!/usr/bin/env python3
"""Compare translated floor vertices against original JP GTE code.

Executes ViewPoint and the floor's batch projector in the CPU/GTE harness.
All matrices and local vertices are synthetic; the harness needs the private
original executable. No original game data is written.
"""
import argparse
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'third_party/Tekken3Remake/tools/research'))
import psxcpu


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--eval',type=Path,required=True)
    parser.add_argument('--cases',type=int,default=100)
    args=parser.parse_args()
    cpu=psxcpu.load_release()
    rng=random.Random(0x4b05c)
    vertices=0x801e0000
    xy=0x801e1000
    depth=0x801e2000
    data=[]
    for z in range(11):
        for x in range(11):
            data.append(((-9000+x*1800)&65535)<<16 | ((-9000+z*1800)&65535))
    cpu.write(vertices,struct.pack('<123I',*(data+[0,0])))
    old_differences=0
    for case in range(args.cases):
        matrix=[rng.randrange(-4096,4097) for _ in range(9)]
        translation=[rng.randrange(-10000,10001),rng.randrange(-5000,5001),rng.randrange(3000,20001)]
        h=rng.randrange(256,801)
        origin=[rng.randrange(-20,21)*1800,0,rng.randrange(-20,21)*1800]
        cpu.write(0x800ae438,struct.pack('<9hh3i',*matrix,0,*translation))
        cpu.gte.write_ctrl(24,192<<16)
        cpu.gte.write_ctrl(25,240<<16)
        cpu.gte.write_ctrl(26,h)
        cpu.call(0x8004b05c,*origin)
        cpu.call(0x8004a5fc,vertices,xy,depth,121)
        observed_xy=struct.unpack('<242h',cpu.read(xy,121*4))
        observed_z=struct.unpack('<121H',cpu.read(depth,121*2))
        expected=[(observed_xy[2*i],observed_xy[2*i+1],observed_z[i]) for i in range(121)]
        values=matrix+translation+[h,origin[0],origin[2]]
        result=subprocess.run([str(args.eval)],input=' '.join(map(str,values))+'\n',
                              text=True,capture_output=True,check=True)
        actual=[tuple(map(int,line.split())) for line in result.stdout.splitlines()]
        if actual!=expected:
            for index,(a,b) in enumerate(zip(actual,expected)):
                if a!=b:
                    raise AssertionError(f'case {case}, vertex {index}, origin {origin}: C {a}, original {b}')
            raise ValueError('evaluator returned wrong vertex count')
        old=subprocess.run([str(args.eval),'--combined'],input=' '.join(map(str,values))+'\n',
                           text=True,capture_output=True,check=True)
        old_values=[tuple(map(int,line.split())) for line in old.stdout.splitlines()]
        if len(old_values)!=121:
            raise ValueError('combined evaluator returned wrong vertex count')
        old_differences+=sum(a!=b for a,b in zip(old_values,expected))
    print(f'floor projection: {args.cases*121} original GTE vertices matched; '
          f'{old_differences} differed with the previous combined transform')


if __name__=='__main__':
    main()
