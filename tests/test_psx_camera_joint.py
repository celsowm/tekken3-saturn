#!/usr/bin/env python3
"""Compare camera/joint composition with the original PS1 function."""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--runner', type=Path, required=True)
    args = parser.parse_args()
    if not (ROOT / 'third_party/Tekken3Remake/work/jp_rev1/exe.bin').is_file():
        return 77
    sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
    try:
        from psxcpu import load_release
    except ImportError:
        return 77
    rng = random.Random(0x80037ab0)
    cpu = load_release()
    view_address, joint_address = 0x80180000, 0x80180100
    rows, expected = [], []
    for i in range(1200):
        bound = 4096 if i < 600 else 32768
        view = [rng.randrange(-bound, bound) for _ in range(9)]
        joint = [rng.randrange(-bound, bound) for _ in range(9)]
        trans = [rng.randrange(-0x80000000, 0x80000000) for _ in range(3)]
        origin = [rng.randrange(-1000000, 1000001) for _ in range(3)]
        h = rng.randrange(65536)
        cpu.write(view_address, struct.pack('<9h2x3i', *view, *trans))
        cpu.write(joint_address, struct.pack('<9h2x3i', *joint, *origin))
        cpu.gte.c[26] = h
        cpu.call(0x80037ab0, view_address, joint_address)
        regs = cpu.gte.c
        matrix = list(struct.unpack('<9h', struct.pack('<5I', *regs[:5])[:18]))
        translation = [struct.unpack('<i', struct.pack('<I', v & 0xffffffff))[0] for v in regs[5:8]]
        expected.append(matrix + translation + [regs[26] & 65535])
        rows.append(view + trans + [h] + joint + origin)
    run = subprocess.run([str(args.runner.resolve())], input=''.join(' '.join(map(str, row)) + '\n' for row in rows), text=True, capture_output=True, check=True)
    actual = [json.loads(line) for line in run.stdout.splitlines()]
    if len(actual) != len(expected):
        raise AssertionError('result count differs')
    for i, (want, got) in enumerate(zip(expected, actual)):
        if want != got:
            raise AssertionError(f'case {i}: expected {want}, got {got}, input {rows[i]}')
    print(f'{len(rows)} camera/joint matrices match original FUN_80037AB0')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
