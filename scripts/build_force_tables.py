#!/usr/bin/env python3
"""Write TABLES.BIN, the constant tables at the start of force.ovl (Japan Rev.1 0x800B0A10, 0x1D0 bytes).

They hold the spawn patterns (6-byte records), the kill-time table, the enemy health per level and
type, the enemy costume keys, the bonus time allowance and the stage bonus of each level, which
src/game/psx_force_level.c reads at their original offsets (see its header for the map). The bytes
are kept as the overlay has them (little-endian). The input is the overlay record (Tekken3Remake
work/jp_rev1/bns/007_rec, or BNS record 7). The output is retail-derived: keep it under
assets/imported or build/.
"""
import argparse
from pathlib import Path

BASE, BYTES = 0x800B0A10, 0x1D0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--overlay', type=Path, required=True, help="force.ovl's BNS record (record 7)")
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    data = args.overlay.read_bytes()[:BYTES]
    if len(data) != BYTES:
        raise ValueError('overlay shorter than its constant tables')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    print(f'{len(data)} bytes -> {args.out}')


if __name__ == '__main__':
    main()
