#!/usr/bin/env python3
"""Print each Tekken Force level's floor kind, kind-0 shade and the distance fade it gives.

Kind 0 (stages.md, Floor kinds; FloorDrawGrid) lights a floor vertex at 128 + table[i] * shade >> 7 (clamped
0-254, 128 unchanged), i = min(beyond * (0x4000000 // fade) >> 16, 1023), beyond = the vertex depth minus the
camera's distance to the fighters, fade = 10,000 units. The 1,024-byte table is at 0x8001DF6C in the executable
(the USA layout matches Rev.1 there). Reads the private LEVn.BIN files and SLUS_004.02; prints only numbers.
"""
import argparse
from pathlib import Path
import struct

FADE_TABLE = 0x8001DF6C
FADE = 10000


def fade_table(exe):
    load = struct.unpack_from('<I', exe, 0x18)[0]
    at = FADE_TABLE - load + 0x800
    return exe[at:at + 1024]


def level_of(table, shade, beyond):
    i = min(beyond * (0x4000000 // FADE) >> 16, 1023)
    return max(0, min(254, 128 + (table[i] * shade >> 7)))


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--levels', type=Path, default=Path('assets/imported/force-packs'))
    p.add_argument('--exe', type=Path, default=Path('assets/imported/tekken3-usa-full/SLUS_004.02'))
    args = p.parse_args()
    table = fade_table(args.exe.read_bytes())
    print('table[0:1024:64]', list(table[0:1024:64]), 'max', max(table))
    for n in range(1, 5):
        b = (args.levels / f'LEV{n}.BIN').read_bytes()
        floor = struct.unpack_from('>16I', b)[10]
        kind, shade = struct.unpack_from('<hh', b, floor + 0x324)
        curve = [level_of(table, shade, d) for d in range(0, 20001, 2000)] if kind == 0 else []
        print(f'level {n}: kind {kind} shade {shade} level by beyond 0..20000 step 2000: {curve}')
    # The Saturn floor shows kind 0 with four palette copies, darkened by 0, 1/12, 2/12 and 3/12 of the shade: the
    # first beyond (units) whose table step rounds to copy k, k = round(table * 12 / 128).
    steps = [next(b for b in range(0, 10001) if round(table[min(b * (0x4000000 // FADE) >> 16, 1023)] * 12 / 128) >= k)
             for k in (1, 2, 3)]
    print('kind-0 copy thresholds (beyond):', steps)


if __name__ == '__main__':
    main()
