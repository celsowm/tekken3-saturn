#!/usr/bin/env python3
"""Export golden cases of Tekken Force's side-scrolling camera for tests/test_psx_force_camera.c.

Runs the original code in the Tekken3Remake CPU harness: force.ovl FUN_800B545C (the set-up) and
FUN_800B5504 (each frame), on random camera records and player positions, and writes inputs and the
camera record afterwards, plus the 0x2800-byte sine table the original reads (retail-derived: it stays
under build/). Run it with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_force_camera_cases.py \
        --out build/force-cases/camera.bin --cases 20000
"""
import argparse
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'third_party/Tekken3Remake/tools/research'))

import verify_menu_sim as vm     # noqa: E402

CAM = 0x800B7098
FIELDS = ('player', 'x', 'y', 'z', 'pitch', 'yaw', 'h', 'mode', 'target_x', 'pitch_target', 'left', 'right', 'min', 'max')
ROLL = 0x800A8A90
LEVEL_FLAG = 0x800AFF74
START_X = 0x800B70E4
PLAYER_VAR = 0x800B6AAC
SINE = 0x8001E8C4
SET_SOURCE, USE_SOURCE = 0x80064308, 0x80064368
PLAYER = 0x800A96F0
MAGIC = 0x5433434D


def record(cpu):
    return list(struct.unpack('<I13i', cpu.read(CAM, 56)))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=20000)
    args = parser.parse_args()
    rng = random.Random(1)
    cpu = vm.menu_cpu('force')
    cpu.r3000_divide_range(0x800B545C, 0x800B58A0)
    cpu.stub(SET_SOURCE, lambda c, *a: 0)
    cpu.stub(USE_SOURCE, lambda c, *a: 0)
    out = bytearray(struct.pack('>3I', MAGIC, 1, args.cases))
    out += cpu.read(SINE, 0x2800)
    for n in range(args.cases):
        if n % 10 == 0:            # the set-up
            flag = rng.choice([0, 3, 1, 2])
            cpu.write(LEVEL_FLAG, struct.pack('<i', flag))
            cpu.write(PLAYER_VAR, struct.pack('<I', PLAYER))
            cpu.write(CAM, bytes(56))
            cpu.write(ROLL, struct.pack('<i', rng.randrange(-5, 300)))
            cpu.call(0x800B545C)
            out += struct.pack('>2i', 0, flag)
            out += struct.pack('>I13i', *record(cpu))
            out += struct.pack('>2i', *struct.unpack('<ii', cpu.read(ROLL, 4) + cpu.read(START_X, 4)))
            continue
        cam = {'player': PLAYER, 'x': rng.randrange(-5000, 60000), 'y': rng.choice([-0xDAC, rng.randrange(-5000, -500)]),
               'z': rng.choice([-0x1B58, rng.randrange(-9000, -500)]), 'pitch': rng.choice([0x2AAA, rng.randrange(0x2AAA, 0x10000)]),
               'yaw': 0, 'h': 500, 'mode': rng.choice([0, 0, 1, 2]), 'target_x': 0, 'pitch_target': rng.choice([0x2AAA, rng.randrange(0x2AAA, 0x10000)]),
               'left': rng.choice([-100, rng.randrange(-300, 0)]), 'right': rng.choice([60, 1, rng.randrange(0, 200)]),
               'min': rng.randrange(-5000, 30000), 'max': rng.randrange(0, 60000)}
        roll = rng.choice([0, 0, 0x82, rng.randrange(0, 4096)])
        px = cam['x'] + rng.choice([rng.randrange(-4000, 4000), rng.randrange(-30000, 30000)])
        pz = rng.randrange(-2500, 2500)
        cpu.write(CAM, struct.pack('<I13i', *(cam[k] for k in FIELDS)))
        cpu.write(ROLL, struct.pack('<i', roll))
        cpu.write(PLAYER + 0xF68, struct.pack('<i', px))
        cpu.write(PLAYER + 0xF70, struct.pack('<i', pz))
        cpu.call(0x800B5504)
        out += struct.pack('>2i', 1, 0)
        out += struct.pack('>I13i', *(cam[k] for k in FIELDS))
        out += struct.pack('>3i', roll, px, pz)
        out += struct.pack('>I13i', *record(cpu))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(out))
    print(f'{args.cases} cases -> {args.out}')


if __name__ == '__main__':
    main()
