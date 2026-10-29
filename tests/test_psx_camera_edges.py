#!/usr/bin/env python3
"""Camera bugs 53/54/55 vs unpatched original phase code, not remake fixes."""
import argparse
import itertools
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner', required=True, type=Path)
    a = p.parse_args()
    if not (RESEARCH.parent.parent / 'work/jp_rev1/exe.bin').is_file():
        print('private original executable absent; skipped')
        return 77
    sys.path.insert(0, str(RESEARCH))
    try:
        from psxcpu import load_release
        from unicorn.mips_const import UC_MIPS_REG_S3
    except ImportError:
        print('original-code harness dependencies absent; skipped')
        return 77
    completed=0
    modes=[]
    for fixed in (False, True):
        cpu = load_release()
        if fixed:
            import fight_harness
            for address, words in fight_harness._fixes():
                for k in range(len(words)):
                    cpu.drop_cop2(address + 4*k)
                cpu.write(address, struct.pack('<' + 'I'*len(words), *words))
        f0, f1, move0, move1 = 0x800A96F0, 0x800AAF7C, 0x801E0000, 0x801E0100
        def put(address, fmt, *values):
            cpu.write(address, struct.pack('<' + fmt, *values))
        switched, chosen = [], []
        def switch(_cpu, side, *args):
            switched.append(side)
        
            return 0
        def choose(_cpu, first, second, player, side, *args):
            chosen.append((player, side))
            return 0xFFFFFFFF
        # Stop after side selection; these routines' later framing/choice work
        # has separate coverage. NormalClip itself executes original COP2 code.
        cpu.stub(0x8006312C, switch)
        cpu.stub(0x80067988, choose)
        cpu.stub(0x800650F8, lambda c, *args: 0)
        cpu.stub(0x80062E5C, lambda c, *args: 0)
        rng = random.Random(0x535455)
        cases = []
        for actor, screen, old0, old1, third in itertools.product(
                range(2), (-32768, 0, 192, 32767), (-1000, 0, 1000), (-1000, 0, 1000), (-1000, 0, 1000)):
            cases.append((53,actor,screen,screen,old0,old1,third,1000,100,2000,500,100,200))
        for kind in (54,55):
            for n in range(500):
                coords=[rng.randrange(-32768,32768) for _ in range(6)]
                cases.append((kind,rng.randrange(2),100,200,100,200,0,*coords))
        expected=[]
        for case in cases:
            kind, actor, sx0, sx1, old0, old1, third, ax, az, bx, bz, cx, cz=case
            cpu.write(f0, bytes(0x188C));cpu.write(f1, bytes(0x188C))
            cpu.write(0x800A9130, bytes(0x80))
            cpu.write(0x800AE170, bytes(0x50))
            put(0x80095890,'h',2);put(0x800AFF50,'i',2);put(0x800AFF04,'i',2000)
            put(0x800A913C,'h',1);put(0x800A9138,'h',1);put(0x800A9154,'h',1)
            put(0x800A9160,'h',-1);put(0x800A9168,'i',-2);put(0x800A916C,'i',-1 if kind!=55 else 0)
            put(0x800A9156,'h',actor);put(0x800A919C,'3h',500,500,500)
            put(0x800AE17C,'i',old0);put(0x800AE18C,'i',old1);put(0x800AE19C,'i',third)
            put(0x800A9140,'5h',4096,0,0,0,0)
            put(0x800A0650,'6i',0,-1000,-8000,0,0,500)
            put(0x800A3E80,'I',1)
            for player, (fighter,move) in enumerate(((f0,move0),(f1,move1))):
                put(fighter+0x54,'I',move);cpu.write(move, bytes(56));put(move+0x24,'I',1)
                put(fighter+0x74,'h',1 if player==actor else -1)
                put(fighter+0x58,'h',1);put(fighter+0x1278,'h',sx1 if player else sx0)
                x,z=(ax,az) if player==actor else (bx,bz)
                put(fighter,'3i',x,0,z);put(fighter+0xF68,'3i',x,0,z)
                put(fighter+0x994,'i',-1000)
            switched.clear();chosen.clear()
            if kind==53:
                put(0x801E0200,'i',2)
                cpu.call(0x800633C0,f0,f1,0x801E0200)
                if len(chosen)!=1:
                    raise AssertionError(f'original must take throw side selection: {case}, {chosen}')
                expected.append(('side',chosen[0][1],chosen[0][0]))
            else:
                thrower,victim=(f1,f0) if actor else (f0,f1)
                move=move1 if actor else move0
                put(0x800A9170,'I',move if kind==55 else 0)
                put(0x800A07D0,'4h',cx,0,cz,0)
                cpu.uc.reg_write(UC_MIPS_REG_S3,1)  # FightFrame's actual saved register.
                value=cpu.call(0x800670E4,0,thrower,victim)&0xFFFFFFFF
                value=value if value<0x80000000 else value-0x100000000
                if kind==54:
                    if len(switched)!=1:
                        raise AssertionError('original stream must call preset switch')
                    expected.append(('clip',switched[0],value))
                else:
                    if switched:
                        raise AssertionError('unset return case must not switch')
                    expected.append(('return',value,struct.unpack('<h',cpu.read(0x800A9138,2))[0]))
        table=ROOT/'assets/imported/tekken3-usa/pose-tables'
        result=subprocess.run([str(a.runner.resolve()),str(table/'psx_trig.bin'),str(table/'psx_atan.bin'),
                               str(table/'psx_body_sqrt.bin'), *(['--fixed'] if fixed else [])],input=''.join(' '.join(map(str,c))+'\n' for c in cases),
                              text=True,capture_output=True,check=True)
        rows=[tuple(map(int,line.split())) for line in result.stdout.splitlines()]
        assert len(rows)==len(cases)
        for case,want,row in zip(cases,expected,rows):
            ok,side,thrower,weight,state=row
            got=('side',side,thrower) if case[0]==53 else ('clip',side,weight) if case[0]==54 else ('return',weight,state)
            if ok!=1 or got!=want:
                raise AssertionError(f'{case}: original {want}, native {row}')
        print(f'{len(cases)} {"remake-fixed" if fixed else "retail"} MIPS camera edge cases matched: '
              f'{sum(c[0]==53 for c in cases)} stale ties, 500 stack-address clips, 500 inherited returns')
        modes.append(rows)
        completed += len(cases)
    assert completed == 2432
    differences={kind:sum(c[0]==kind and raw[column]!=fixed[column]
                         for c,raw,fixed in zip(cases,*modes))
                 for kind,column in ((53,1),(54,1),(55,3))}
    assert all(differences.values()), 'every original bug must distinguish the two modes'
    print(f'Retail/fixed decisions differ: {differences}')
    return 0


if __name__=='__main__':
    sys.exit(main())
