#!/usr/bin/env python3
"""Export golden cases of Tekken Ball's court bounds (ArenaBounds in mode 7) for tests/test_ball_court.c.

Runs the pinned Tekken3Remake's verified port (`fight_sim.arena_bounds` with the game mode set to 7) on random fighters,
one correction per case. Per case (big-endian int32): the fighter index, whether it is a throw victim (the ramp restarts),
the ramp, the anchor (x, y, z) and the root (x, y, z); then the anchor and root afterwards and whether the fighter was set to
state 0x5000. Run it with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_ball_court_cases.py --out build/ball-cases/court.bin
"""
import argparse
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import fight_sim as fs           # noqa: E402

MAGIC = 0x54334354
FIGHTERS = (0x800A96F0, 0x800AAF7C)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=4000)
    parser.add_argument('--seed', type=int, default=3)
    args = parser.parse_args()
    rnd = random.Random(args.seed)
    out = bytearray(struct.pack('>3I', MAGIC, 1, args.cases))
    for _ in range(args.cases):
        ram = fs.Ram(bytes(0x200000))
        ram.put(fs.GAME_MODE, 'I', 7)
        index = rnd.randint(0, 1)
        f = fs.Fighter(ram, FIGHTERS[index])
        o = fs.Fighter(ram, FIGHTERS[1 - index])
        f.index, o.index = index, 1 - index
        victim = rnd.random() < 0.1
        ramp = rnd.choice([0, 2, 10, 600, 2000, 2000])
        rx = rnd.choice([rnd.randint(-0x2000, 0x2000), rnd.randint(-1100, 1100), rnd.randint(-0x1500, 0x1500)])
        ry = rnd.choice([rnd.randint(-0x1400, 0), rnd.randint(-0x1300, -0x1100), rnd.randint(-2000, 0)])
        rz = rnd.randint(-0x300, 0x300)
        ax, ay, az = (v + rnd.randint(-50, 50) for v in (rx, ry, rz))
        f.rootX, f.rootY, f.rootZ = rx, ry, rz
        f.posX, f.posY, f.posZ = ax, ay, az
        f.state = rnd.randint(0, 0x4000)
        f.inThrow = 0
        f.throwState = -1 if victim else 0
        ram.put(fs.BOUND_RAMP + 4 * index, 'i', ramp)
        before = (index, int(victim), ramp, ax, ay, az, rx, ry, rz)
        fs.arena_bounds(f)
        # the ramp the C caller reset before the call
        if victim:
            before = (index, 1, ramp, ax, ay, az, rx, ry, rz)
        after = (f.posX, f.posY, f.posZ, f.rootX, f.rootY, f.rootZ, int(f.state == 0x5000))
        out += struct.pack('>9i7i', *before, *after)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f'{args.cases} cases -> {args.out}')


if __name__ == '__main__':
    main()
