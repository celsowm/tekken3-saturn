#!/usr/bin/env python3
"""Compare the floor-grid centre with the original JP Rev.1 or USA CPU routine.

Requires the private original executable and lookup tables. No game data is
written; the CPU harness replaces only FloorDrawGrid with a centre capture.
"""
import argparse
import hashlib
import random
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
import psxcpu
from fight_oracle_export import Layout


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--eval', type=Path, required=True)
    p.add_argument('--trig', type=Path, required=True)
    p.add_argument('--body', type=Path, required=True)
    p.add_argument('--cases', type=int, default=2000)
    p.add_argument('--usa-exe', type=Path, help='verify against the qualified USA executable')
    args = p.parse_args()
    rng = random.Random(0x49290)
    cases = []
    for _ in range(args.cases):
        pitch = rng.choice([rng.randrange(-500, 900), 0, -37, -38, 785, 1024])
        yaw = rng.randrange(4096)
        cases.append((pitch, yaw, rng.randrange(-3000, 3001), rng.randrange(-8000, -100),
                      rng.randrange(-10000, 10001), rng.randrange(-3000, 3001), rng.randrange(-3000, 3001)))
    # Exercise the pitch gate, height cutoff and octant boundaries explicitly.
    for pitch in (-240, -239, -238, -38, -37, -36, 0, 1, 784, 785, 786, 1024, 1808, 1809):
        for yaw in (0, 1, 511, 512, 513, 1023, 1024, 1025, 1536, 2048, 2560, 3072, 3584, 4095):
            for y in (-8000, -1721, -1720, -1719):
                cases.append((pitch, yaw, 1200, y, -5500, -1350, 900))
    for _ in range(1000):
        cases.append((rng.randrange(1750, 1810), rng.randrange(4096),
                      rng.randrange(-3000, 3001), rng.randrange(-8000, -1720),
                      rng.randrange(-10000, 10001), rng.randrange(-3000, 3001), rng.randrange(-3000, 3001)))
    data = ''.join(' '.join(map(str, case)) + '\n' for case in cases)
    exe_path=args.usa_exe or ROOT / 'third_party/Tekken3Remake/work/jp_rev1/exe.bin'
    sqrt_offset = 0x800 + (0x3af40 if args.usa_exe else 0x3b1d8)
    result = subprocess.run([str(args.eval), str(args.trig), str(args.body),str(exe_path),str(sqrt_offset)], input=data,
                            text=True, capture_output=True, check=True)
    actual = [tuple(map(int, line.split())) for line in result.stdout.splitlines()]
    if len(actual) != len(cases):
        raise ValueError('evaluator returned the wrong number of cases')
    if args.usa_exe:
        exe=args.usa_exe.read_bytes()
        if hashlib.sha256(exe).hexdigest() != 'fbda8b68e5799dbef4af39a161783bc670c15b0aa0e87dce65e210717da19b8c':
            raise ValueError('unexpected USA executable')
        layout=Layout(ROOT / 'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json')
        mapped=layout.to_usa
        sites=psxcpu.read_sites(ROOT / 'third_party/Tekken3Remake/work/decomp/SLPS_013.00.cop2.txt',
                                0x8004b174,0x8004b1d8)
        cpu=psxcpu.PsxCpu(exe,exe_cop2={mapped(site) for site in sites})
    else:
        mapped=lambda address: address
        cpu=psxcpu.load_release()
    if args.trig.read_bytes() != cpu.read(mapped(0x8001e8c4), 10240):
        raise ValueError('trigonometry lookup differs from the selected executable')
    if args.body.read_bytes()[:384] != cpu.read(mapped(0x8004b1d8), 384):
        raise ValueError('SquareRoot0 lookup differs from the selected executable')
    captured = []
    def floor_grid(c, address, _a1, _a2, _a3):
        x, _, z = struct.unpack('<3i', c.read(address, 12))
        captured.append((1, x, z))
        return 0
    cpu.stub(mapped(0x80049290), floor_grid)
    for addr, value in ((0x8009eb28, 1800), (0x8009ebcc, 10), (0x8009ebd0, 10), (0x800aff50, 0)):
        cpu.write(mapped(addr), struct.pack('<i', value))
    compared = skipped = 0
    unsupported_original = []
    for case, got in zip(cases, actual):
        pitch, yaw, x, y, z, mx, mz = case
        cpu.write(mapped(0x800a8a88), struct.pack('<ii', pitch, yaw))
        cpu.write(mapped(0x800a8a9c), struct.pack('<3i', x, y, z))
        for fighter in (0x800a96f0, 0x800aaf7c):
            cpu.write(mapped(fighter) + 0xf68, struct.pack('<3i', mx, 0, mz))
        captured.clear()
        cpu.call(mapped(0x800490f0), mapped(0x800a96f0), mapped(0x800aaf7c))
        expected = captured[0] if captured else (0, 0, 0)
        if got[0] == -1:
            skipped += 1
            unsupported_original.append((case, expected))
            continue
        if got != expected:
            raise AssertionError(f'case {case}: C {got}, original {expected}')
        compared += 1
    print(f'floor centre: {compared} original-code matches; {skipped} unsupported singular/overflow cases')
    if unsupported_original:
        pitches = [case[0] for case, _ in unsupported_original]
        print(f'unsupported pitch range: {min(pitches)}..{max(pitches)}')


if __name__ == '__main__':
    main()
