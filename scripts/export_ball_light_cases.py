#!/usr/bin/env python3
"""Export golden cases of Tekken Ball's ball lighting for tests/test_ball_light.c.

Runs the pinned Tekken3Remake's verified port (`projection_sim.color_mat_col`, volley.ovl FUN_800B3198's ColorMatCol on its
GTE) with the light matrix and colour matrix the stage sets up (row 0 the doubled main light, row 1 the fill light from below;
colour matrix column 0 the light colour << 4, column 1 half of it), the type's back colour and specular power, on random
normals, stage lights and face colours. Per case (big-endian int32): light direction (3), light colour (3), back colour (3),
power, fill, normal (3), face colour (3), the lit colour (3). Run it with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/export_ball_light_cases.py --out build/ball-cases/light.bin
"""
import argparse
import random
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import projection_sim as pj      # noqa: E402
from fight_sim import Ram        # noqa: E402
from psxcpu import GTE           # noqa: E402

MAGIC = 0x54334C54               # 'T3LT'
VEC, COL, OUT = 0x801FC000, 0x801FC010, 0x801FC020


def set_matrix(gte, base, rows):
    """SetLightMatrix / SetColorMatrix: nine halfwords, row by row, into five control words."""
    flat = [v & 0xFFFF for row in rows for v in row]
    words = [flat[0] | flat[1] << 16, flat[2] | flat[3] << 16, flat[4] | flat[5] << 16, flat[6] | flat[7] << 16, flat[8]]
    for i, w in enumerate(words):
        gte.write_ctrl(base + i, w)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cases', type=int, default=6000)
    parser.add_argument('--seed', type=int, default=11)
    args = parser.parse_args()
    rnd = random.Random(args.seed)
    out = bytearray(struct.pack('>3I', MAGIC, 1, args.cases))
    for _ in range(args.cases):
        ram = Ram(bytes(0x200000))
        gte = GTE()
        light = [rnd.randrange(-4096, 4097) for _ in range(3)]
        colour = [rnd.choice([rnd.randrange(256), 200, 128]) for _ in range(3)]
        back = [rnd.choice([rnd.randrange(256), 0x70, 0x10]) for _ in range(3)]
        power = rnd.choice([0, 1, 1, 2, 3])
        fill = rnd.choice([0, 1, 1])
        normal = [rnd.choice([rnd.randrange(-4096, 4097), rnd.randrange(-300, 301)]) for _ in range(3)]
        face = [rnd.randrange(256) for _ in range(3)]
        doubled = [(2 * v) & 0xFFFF for v in light]
        llm = [[v - 0x10000 if v >= 0x8000 else v for v in doubled], [0, 4096, 0], [0, 0, 0]]
        lcm = [[c << 4, (c >> 1) << 4 if fill else 0, 0] for c in colour]
        set_matrix(gte, 8, llm)
        set_matrix(gte, 16, lcm)
        pj.set_back_color(gte, *back)
        for i, v in enumerate(normal):
            ram.put(VEC + 2 * i, 'H', v & 0xFFFF)
        ram.put(COL, 'I', face[0] | face[1] << 8 | face[2] << 16)
        pj.color_mat_col(ram, gte, VEC, COL, OUT, power)
        lit = [ram.u8(OUT + i) for i in range(3)]
        out += struct.pack('>20i', *light, *colour, *back, power, fill, *normal, *face, *lit)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f'{args.cases} cases -> {args.out}')


if __name__ == '__main__':
    main()
