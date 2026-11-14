#!/usr/bin/env python3
"""Extract practice's combo tables (COMBO TRAINING) from a practice overlay into a private blob and a C source.

The tables are practice.ovl's (Japan Rev.1 layout, as Tekken3Remake's tools/remake_import/flow.py reads them): per
bank type a list of combos, each a list of key-ring steps and the move slots the player must perform. The blob is

    "T3CB", u16 kinds, then per kind: u16 count, per combo: u16 steps, u16 moves, u32[steps], u16[moves]

(little endian). Output is derived from the game and stays out of git (assets/imported/).

    scripts/extract_practice_combos.py --overlay third_party/Tekken3Remake/work/jp_rev1/bns/006_practice.ovl \\
        --out assets/imported/practice-probe/practice-combos
"""
import argparse
import struct
from pathlib import Path

BASE = 0x800B0A10          # the mode overlays' load address (Japan Rev.1)
COMBOS = (0x800B2AD8, 17)  # per bank type: (combo records, count), 8 bytes each


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--overlay', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True, help='prefix: writes <out>.bin and <out>.c')
    args = ap.parse_args()
    data = args.overlay.read_bytes()

    def u32(addr):
        return struct.unpack_from('<I', data, addr - BASE)[0]

    blob = bytearray(b'T3CB' + struct.pack('<H', COMBOS[1]))
    total = 0
    for kind in range(COMBOS[1]):
        table, count = u32(COMBOS[0] + 8 * kind), u32(COMBOS[0] + 8 * kind + 4)
        blob += struct.pack('<H', count)
        for c in range(count):
            r = table + 12 * c
            steps, moves, counts = u32(r), u32(r + 4), u32(r + 8)
            n_steps, n_moves = counts & 0xFFFF, counts >> 16
            blob += struct.pack('<HH', n_steps, n_moves)
            blob += b''.join(struct.pack('<I', u32(steps + 4 * k)) for k in range(n_steps))
            blob += struct.pack(f'<{n_moves}H', *struct.unpack_from(f'<{n_moves}H', data, moves - BASE))
            total += 1
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.with_suffix('.bin').write_bytes(blob)
    lines = ['/* Derived from the game by scripts/extract_practice_combos.py; private, not in git. */',
             '#include <stdint.h>', f'const uint32_t t3_practice_combo_blob_size={len(blob)}u;',
             'const uint8_t t3_practice_combo_blob[]={']
    for i in range(0, len(blob), 16):
        lines.append(','.join(str(b) for b in blob[i:i + 16]) + ',')
    lines.append('};')
    args.out.with_suffix('.c').write_text('\n'.join(lines) + '\n')
    print(f'{COMBOS[1]} bank types, {total} combos, {len(blob)} bytes -> {args.out}.bin/.c')


if __name__ == '__main__':
    main()
