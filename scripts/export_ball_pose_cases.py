#!/usr/bin/env python3
"""Export golden cases of Tekken Ball's ball and shadow matrices for tests/test_ball_pose.c.

Runs the pinned Tekken3Remake's verified port (`ball_sim.ball_draw` and `_shadow`'s matrix steps, volley.ovl FUN_800B3198, on
projection_sim's RotMatrix, MulMatrix, MulMatrix2 and TransposeMatrix) on random spin angles, squash axes and squashes, and
reads back the matrices. Per case (big-endian int32): the spin angles (3), the squash axis angles (3), the squash, the
ball's matrix (9), the shadow's matrix (9). The file begins with the executable's sine table (10,240 bytes), so it is
retail-derived and stays under build/. Run it with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_ball_pose_cases.py --out build/ball-cases/pose.bin
"""
import argparse
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import projection_sim as pj      # noqa: E402
from fight_sim import Ram       # noqa: E402
from psxcpu import GTE           # noqa: E402

MAGIC = 0x54335042               # 'T3PB'
EXE_BASE = 0x80010000
TRIG_BYTES = 10240
SCRATCH = 0x801FC000


def diagonal(ram, m, q):
    """ball_sim._diagonal."""
    for i, v in enumerate((0x1000 - q, 0, 0, 0, q + 0x1000, 0, 0, 0, q + 0x1000)):
        ram.put(m + 2 * i, 'H', v & 0xFFFF)


def matrix(ram, at):
    return [ram.s16(at + 2 * i) for i in range(9)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=4000)
    parser.add_argument('--seed', type=int, default=7)
    args = parser.parse_args()
    rnd = random.Random(args.seed)
    exe = (ROOT / 'third_party/Tekken3Remake/work/jp_rev1/exe.bin').read_bytes()[0x800:]
    trig = exe[pj.SIN_TABLE - EXE_BASE:pj.SIN_TABLE - EXE_BASE + TRIG_BYTES]
    out = bytearray(struct.pack('>3I', MAGIC, 1, args.cases) + trig)
    m, squash, ang, axis, trans = SCRATCH, SCRATCH + 0x20, SCRATCH + 0x40, SCRATCH + 0x60, SCRATCH + 0x80
    for _ in range(args.cases):
        ram = Ram(bytes(0x200000))
        ram.data[ram._off(EXE_BASE):ram._off(EXE_BASE) + len(exe)] = exe
        gte = GTE()
        spin = [rnd.choice([rnd.getrandbits(16), rnd.randrange(4096)]) for _ in range(3)]
        axes = [0, rnd.getrandbits(16), rnd.getrandbits(16)]
        q = rnd.choice([0, 0, rnd.randrange(-0x400, 0x400), rnd.randrange(-0x4000, 0x4000)])
        # the ball (ball_draw)
        for i, v in enumerate(spin):
            ram.put(ang + 2 * i, 'H', v & 0xFFFF)
        pj.rot_matrix_angles(ram, gte, ang, m)
        if q != 0:
            diagonal(ram, squash, q)
            for i, v in enumerate(axes):
                ram.put(ang + 2 * i, 'H', v & 0xFFFF)
            pj.rot_matrix_angles(ram, gte, ang, axis)
            pj.transpose_matrix(ram, axis, trans)
            pj.mul_matrix(ram, gte, axis, squash)
            pj.mul_matrix(ram, gte, axis, trans)
            pj.mul_matrix2(ram, gte, axis, m)
        ball = matrix(ram, m)
        # the shadow (_shadow)
        for i, v in enumerate((0, spin[1], 0)):
            ram.put(ang + 2 * i, 'H', v & 0xFFFF)
        pj.rot_matrix_angles(ram, gte, ang, m)
        diagonal(ram, squash, q)
        for i, v in enumerate((0, axes[1], 0)):
            ram.put(ang + 2 * i, 'H', v & 0xFFFF)
        pj.rot_matrix_angles(ram, gte, ang, axis)
        pj.mul_matrix(ram, gte, axis, squash)
        pj.mul_matrix2(ram, gte, axis, m)
        shadow = matrix(ram, m)
        out += struct.pack('>7i18i', *spin, *axes, q, *ball, *shadow)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f'{args.cases} cases -> {args.out}')


if __name__ == '__main__':
    main()
