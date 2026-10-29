#!/usr/bin/env python3
"""Eye timer and shared rand consumption against original FUN_80034354."""
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
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True, type=Path)
    args = parser.parse_args()
    if not (RESEARCH.parent.parent / 'work/jp_rev1/exe.bin').is_file():
        print('private original executable absent; skipped')
        return 77
    sys.path.insert(0, str(RESEARCH))
    try:
        from psxcpu import load_release
    except ImportError:
        print('original-code harness dependencies absent; skipped')
        return 77
    cpu = load_release()
    fighter, move = 0x800A96F0, 0x801E0000
    # Texture upload output is irrelevant to the eye/RNG state comparison.
    cpu.stub(0x80029610, lambda c, *a: 0)
    cpu.write(0x80095DF0, bytes([255] * 4))
    cpu.write(fighter + 0x12, bytes(2))
    cpu.write(fighter + 0x18, bytes(2))
    cpu.write(fighter + 0x1C, bytes(2))
    cpu.write(fighter + 0x54, struct.pack('<I', move))
    rng = random.Random(0x34354)
    cases = [(t, c, f, s, rng.getrandbits(32)) for t,c,f,s in itertools.product(
        (-32768, -1, 0, 1, 2, 3, 4, 255, 32767), (0, 1, 2),
        (0, 0x40000, 0x40000000, 0x40040000), (0, 4, 0xFFFFFFFF))]
    cases += [(rng.randrange(-32768,32768),rng.randrange(256),rng.getrandbits(32),
               rng.getrandbits(32),rng.getrandbits(32)) for _ in range(300)]
    expected = []
    for timer,closed,flags,state,seed in cases:
        cpu.write(fighter + 0x128E, struct.pack('<hB',timer,closed))
        cpu.write(move + 4, struct.pack('<I',state))
        cpu.write(move + 0x24, struct.pack('<I',flags))
        cpu.write(0x800A3E80, struct.pack('<I',seed))
        cpu.call(0x80034354,fighter)
        expected.append((*struct.unpack('<hB',cpu.read(fighter+0x128E,3)),
                         struct.unpack('<I',cpu.read(0x800A3E80,4))[0]))
    result = subprocess.run([str(args.runner.resolve())], text=True, capture_output=True,
        input=''.join(' '.join(map(str,row))+'\n' for row in cases),check=True)
    actual = [tuple(map(int,line.split())) for line in result.stdout.splitlines()]
    if len(actual) != len(expected):
        raise AssertionError('wrong native output count')
    for case,want,got in zip(cases,expected,actual):
        if want != got:
            raise AssertionError(f'{case}: original {want}, native {got}')
    print(f'{len(cases)} original MIPS eye timer/RNG cases matched')
    return 0


if __name__ == '__main__':
    sys.exit(main())
