#!/usr/bin/env python3
"""Export golden cases of Tekken Force's roles (who faces whom) for tests/test_psx_force_roles.c.

Runs the pinned Tekken3Remake's verified `force_sim.fighter_roles` (force.ovl FUN_800B5EC0, with FUN_800B598C) on random
states, with the fighters' relative angles given instead of computed (the match core has them), and
writes each case's inputs and expected pick and state. Run it with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_force_roles_cases.py \
        --out build/force-cases/roles.bin --cases 30000

The output holds no game data, only random states and the port's answers; it stays under build/.
"""
import argparse
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'third_party/Tekken3Remake/tools/research'))

import fight_sim                 # noqa: E402
import force_sim as fs           # noqa: E402
from fight_sim import Ram       # noqa: E402

MAGIC = 0x54335247  # 'T3RG'
WORDS_IN = 27
WORDS_OUT = 7


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=30000)
    parser.add_argument('--seed', type=int, default=1)
    args = parser.parse_args()
    rng = random.Random(args.seed)
    sp = lambda v: v if v < 0x8000 else v - 0x10000  # noqa: E731
    F = (fs.F0, fs.F1, fs.F2)
    angles = {}

    def relative_angles(fp, fo):
        quadrant, rel = angles[fo.base]
        fp.ram.put(F[0] + 0x32, 'H', quadrant)
        fp.ram.put(F[0] + 0x30, 'H', rel)
    fight_sim.relative_angles = relative_angles
    out = bytearray(struct.pack('>4I', MAGIC, 1, WORDS_IN, args.cases))
    for _ in range(args.cases):
        ram = Ram(bytes(0x200000))
        wait = rng.choice([0, 0, 1, 5, 0x78, -3])
        manual = rng.choice([0, 0, 1])
        side = rng.randrange(2)
        button = rng.choice([1, 2, 4, 8])
        pad = rng.choice([0, button, rng.getrandbits(16)])
        prev = rng.choice([1, 2])
        hidden = [rng.choice([0, 0, 0, 1, 2]) for _ in range(3)]
        screen = [rng.choice([0, 1, 200, 399, 400, 0x191, rng.getrandbits(16)]) for _ in range(3)]
        pair = [rng.choice([rng.randrange(0, 0x1400), rng.randrange(-0x1400, 0x1400)]) for _ in range(5)]
        if rng.random() < 0.5:
            pair[1 + prev] = 0x9C4 + rng.randrange(-0x50, 0x50)
        order = [rng.randrange(4) for _ in range(4)]
        quad = [0, rng.randrange(4), 0]
        quad[2] = quad[1] if rng.random() < 0.5 else rng.randrange(4)
        rel = [0, rng.getrandbits(16), rng.getrandbits(16)]
        if rng.random() < 0.3:
            rel[2] = (rel[1] + rng.choice([0, 0xFFF, 0x1000, 0x1FFF, 0x2000, 0x2FFF, 0x3000, 0x3001])) & 0xFFFF
        angles[F[1]], angles[F[2]] = (quad[1], rel[1]), (quad[2], rel[2])
        ram.put(0x800AFF50, 'i', 8)
        ram.put(fs.TARGET_WAIT, 'i', wait)
        ram.put(fs.MANUAL, 'I', manual)
        ram.put(fs.MANUAL_SIDE, 'I', side)
        ram.put(fs.MANUAL_BUTTON, 'I', button)
        human = rng.randrange(2)
        ram.put(fs.HUMAN_SIDE, 'H', human)
        ram.put(fs.PAD_PRESSED + (2 if human else 0), 'H', pad)
        ram.put(F[0] + 0x20, 'B', prev)
        for k in range(3):
            ram.put(F[k] + 0x1E, 'B', k)
            ram.put(F[k] + 0xC2, 'B', hidden[k])
            ram.put(F[k] + 0x1278, 'H', screen[k])
        for k, v in enumerate(pair):
            ram.put(0x8009EA08 + 0x10 * k, 'i', v)
        for k, v in enumerate(order):
            ram.put(fs.QUADRANT_ORDER + 4 * k, 'i', v)
        fs.fighter_roles(ram, 0x801F0000)
        out += struct.pack(f'>{WORDS_IN}i', wait, manual, side, button, sp(pad), prev, *hidden, *screen, *quad, *[sp(r) for r in rel], *pair, *order)
        out += struct.pack('>7i', ram.u8(F[0] + 0x20), ram.u8(F[1] + 0x20), ram.u8(F[2] + 0x20), ram.u8(F[0] + 0xC7),
                           ram.s32(fs.TARGET_WAIT), ram.s32(fs.MANUAL_SIDE), F.index(ram.u32(0x801F0004)))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(out))
    print(f'{args.cases} cases -> {args.out}')


if __name__ == '__main__':
    main()
