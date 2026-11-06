#!/usr/bin/env python3
"""Export golden cases of the Tekken Force walls for tests/test_psx_force_walls.c: the pinned
Tekken3Remake's verified `fight_sim._force_walls` on random positions, camera and levels.

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_force_walls_cases.py \
        --out build/force-cases/walls.bin --cases 30000

Holds no game data (random states and the port's answers); stays under build/.
"""
import argparse
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'third_party/Tekken3Remake/tools/research'))

import fight_sim                 # noqa: E402
from fight_sim import Ram       # noqa: E402

LINES = {0: (-323, -2553, 329, 1971, -259, -2285), 3: (-375, -2445, 333, 2175, -259, -2285)}


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=30000)
    args = parser.parse_args()
    rng = random.Random(1)
    out = bytearray(struct.pack('>3I', 0x54335753, 1, args.cases))
    base = 0x800A96F0
    for _ in range(args.cases):
        ram = Ram(bytes(0x200000))
        level = rng.randrange(4)
        lines = LINES[3 if level == 3 else 0]
        for k, v in enumerate(lines):
            ram.put(fight_sim.FORCE_WALLS + 4 * k, 'i', v)
        view = rng.choice([rng.randrange(-0x2000, 0x40000), 0])
        ram.put(fight_sim.FORCE_VIEW_X, 'i', view)
        ram.put(0x800AE14C, 'H', 15 + level)
        x = view + rng.choice([rng.randrange(-0x2400, 0x2400), rng.randrange(-0x8000, 0x8000)])
        z = rng.choice([rng.randrange(-0xC00, 0xC00), rng.randrange(-0x3000, 0x3000)])
        enemy = rng.randrange(2)
        ram.put(base + 0xF68, 'i', x)
        ram.put(base + 0xF70, 'i', z)
        ram.put(base + 0xC5, 'B', enemy)
        dz, dx = fight_sim._force_walls(ram, fight_sim.Fighter(ram, base))
        out += struct.pack('>7i', level, x, z, enemy, view, dx, dz)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(bytes(out))
    print(f'{args.cases} cases -> {args.out}')


if __name__ == '__main__':
    main()
