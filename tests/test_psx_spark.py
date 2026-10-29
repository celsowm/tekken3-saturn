#!/usr/bin/env python3
"""Spark phases/trajectories and RNG vs unpatched original code, not pool/render proof."""
import argparse
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'
FUNCS = {1: 0x800700EC, 2: 0x800704D8, 12: 0x8007379C, 13: 0x80073B94,4:0x80070E5C,5:0x80071188}
MODELS = {0: 0, 1: 0x8002554C, 2: 0x8002593C,3:0x8002556C,4:0x800256E0,5:0x80025700}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    args = parser.parse_args()
    exe = RESEARCH.parent.parent / 'work/jp_rev1/exe.bin'
    if not exe.is_file():
        print('private original executable absent; skipped')
        return 77
    sys.path.insert(0, str(RESEARCH))
    try:
        from psxcpu import load_release
    except ImportError:
        print('original MIPS dependencies absent; skipped')
        return 77
    cpu = load_release()
    cpu.call(0x8006E848)  # Actual original palette-ramp initialization.
    base = 0x80180000
    cases = []
    for kind in FUNCS:
        for seed in [*range(16), 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF]:
            for steps in (1, 2, 9, 10, 31, 32, 51, 52, 55):
                cases.append([seed, steps, seed & 1, kind, 0x80, 0, 0, -32768, 32767, 0,
                              0x1234, 0x5678, 10, -20000, -10, 0, 0, 0, 0, 0, 0, 0])
    rng = random.Random(0x700EC)
    for _ in range(700):
        kind = rng.choice(list(FUNCS))
        cases.append([rng.getrandbits(32), rng.choice([1, 2, 10, 55]), rng.randrange(2), kind,
                      rng.choice([0x80, 0x88, 0xC0, 0xFF]), rng.choice([0, 1, 1, 2, 255]),
                      rng.randrange(3), rng.randrange(-32768, 32768), rng.randrange(-32768, 32768),
                      rng.randrange(-8192, 8192), rng.randrange(65536), rng.randrange(65536),
                      rng.choice([-2147483648, -100, 0, 2147483647]),
                      rng.choice([-20000, -100, -1, 0, 1]), rng.randrange(-1000, 1000),
                      rng.randrange(-0x1000000, 0x1000000), rng.randrange(-0x1000000, 0x1000000),
                      rng.randrange(-0x1000000, 0x1000000), rng.randrange(52), rng.randrange(1, 53),
                      rng.randrange(512), rng.randrange(0x680, 0x880)])
    expected, updates = [], 0
    for case in cases:
        seed, steps, buffer, kind, flags, state, model, pitch, yaw, speed, clut0, clut1, *values = case
        raw = bytearray(0xA0)
        struct.pack_into('<I', raw, 8, MODELS[model])
        struct.pack_into('<3B', raw, 12, kind, flags, state)
        struct.pack_into('<hh', raw, 0x10, pitch, yaw)
        struct.pack_into('<h', raw, 0x18, speed)
        x, y, z, vx, vy, vz, a, b, c, d = values
        struct.pack_into('<6i', raw, 0x1C, vx, vy, vz, x, y, z)
        struct.pack_into('<4i', raw, 0x40, a, b, c, d)
        struct.pack_into('<H', raw, 0x5E, clut0)
        struct.pack_into('<H', raw, 0x86, clut1)
        cpu.write(base, raw)
        cpu.write(0x800A3E80, struct.pack('<I', seed))
        for n in range(steps):
            if cpu.read(base + 12, 1)[0] == 0:
                break
            cpu.write(0x800AE3C4, struct.pack('<I', (buffer + n) & 1))
            cpu.call(FUNCS[kind], base)
            updates += 1
        raw = bytes(cpu.read(base, 0xA0))
        seed = struct.unpack('<I', cpu.read(0x800A3E80, 4))[0]
        kind, flags, state = struct.unpack_from('<3B', raw, 12)
        model_pointer = struct.unpack_from('<I', raw, 8)[0]
        model = {value: key for key, value in MODELS.items()}[model_pointer]
        pitch, yaw = struct.unpack_from('<hh', raw, 0x10)
        speed = struct.unpack_from('<h', raw, 0x18)[0]
        vx, vy, vz, x, y, z = struct.unpack_from('<6i', raw, 0x1C)
        a, b, c, d = struct.unpack_from('<4i', raw, 0x40)
        clut0, clut1 = (struct.unpack_from('<H', raw, off)[0] for off in (0x5E, 0x86))
        expected.append([seed, kind, flags, state, model, pitch, yaw, speed, clut0, clut1,
                         x, y, z, vx, vy, vz, a, b, c, d])
    result = subprocess.run([str(args.runner.resolve()), str(exe)], input=''.join(
        ' '.join(map(str, case)) + '\n' for case in cases), text=True, capture_output=True, check=True)
    actual = [json.loads(line) for line in result.stdout.splitlines()]
    assert len(actual) == len(expected), (len(actual), len(expected))
    fields = 'random type flags state model pitch yaw speed clut0 clut1 x y z vx vy vz a b c d'.split()
    for n, (want, got) in enumerate(zip(expected, actual)):
        if want != got:
            raise AssertionError(dict(case=n, input=cases[n], differences={
                key: [x, y] for key, x, y in zip(fields, want, got) if x != y}))
    print(json.dumps(dict(matched_cases=len(cases), original_updates=updates, types=list(FUNCS),
                          fields=fields, scope='Object phases and trajectories, not allocation/rendering or a passing fight')))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
