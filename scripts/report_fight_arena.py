#!/usr/bin/env python3
"""Low WRAM left in the fighter arena for every pairing, from the F*.BIN pack headers on a built disc.

The arena (fighter_pack.c g_arena) holds the DIVMOT banks and camera streams of the two fighters and is sized for the
worst pairing. Any other pairing leaves a tail unused. This reports that tail, and the pairings that leave less than
--need bytes, so a feature that borrows the tail can say whether it fits for every pair.

    python3 scripts/report_fight_arena.py --iso-root build/disc/iso_root --need 116000"""
import argparse
import struct
from pathlib import Path

HEADER_WORDS, DIRECTORY_WORD = 64, 26
BANK, CAMERA = 2, 3


def read_pack(path):
    w = struct.unpack('>%dI' % HEADER_WORDS, path.read_bytes()[:HEADER_WORDS * 4])
    section = lambda s: w[DIRECTORY_WORD + 2 * s + 1]
    return dict(bank_type=w[3], bank=section(BANK), camera=section(CAMERA))


def align(n):
    return (n + 15) & ~15


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--iso-root', type=Path, required=True)
    ap.add_argument('--need', type=int, default=0, help='bytes a feature wants from the arena tail')
    args = ap.parse_args()
    packs = [read_pack(p) for p in sorted(args.iso_root.glob('F[0-9][0-9].BIN'))]

    def used(a, b):
        return (align(a['bank']) + (0 if a['bank_type'] == b['bank_type'] else align(b['bank'])) +
                align(a['camera']) + align(b['camera']))
    pairs = [(used(a, b), i, j) for i, a in enumerate(packs) for j, b in enumerate(packs) if i != j or True]
    arena = max(u for u, _, _ in pairs)
    print(f'{len(packs)} packs, arena {arena} bytes (worst pairing)')
    free = sorted((arena - u, i, j) for u, i, j in pairs)
    print(f'least tail {free[0][0]} bytes (pair {free[0][1]},{free[0][2]}); median {free[len(free)//2][0]}; '
          f'most {free[-1][0]}')
    short = [(f, i, j) for f, i, j in free if f < args.need]
    print(f'pairings leaving less than {args.need} bytes: {len(short)} of {len(pairs)}')
    for f, i, j in short[:12]:
        print(f'  {i:2d} vs {j:2d}: {f} bytes')


if __name__ == '__main__':
    main()
