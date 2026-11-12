#!/usr/bin/env python3
"""Extract private Force chicken/shadow sprites from the four USA stage archives.

Texture rectangles follow Tekken3Remake tools/remake_import/stage.py and
force.ovl FUN_800B4374 (PolyForm Noncommercial; NOTICE.md).
Required Notice: Copyright (c) 2026 n0madic.

ITEMn.BIN: T3FI0001, four BE u16 dimensions (64,128,64,64), two
16-colour BE Saturn palettes, 4096 LUT4 picture bytes, 4096 INDEX8 shadow
bytes. Shadow colours include the original 0x20/0x80 texture modulation;
its half transparency is applied by the renderer. All output is private.
"""
import argparse
from pathlib import Path
import struct

from build_force_levels import read_stage
from psx_character_textures import saturn_colour


def build(arc):
    _, vram = read_stage(arc)
    palettes, pixels = [], []
    for y, height, clut, shade in ((64, 128, 0x7801, False), (0, 64, 0x7800, True)):
        cx, cy = (clut & 63) * 16, clut >> 6
        colours = vram[cy * 1024 + cx:cy * 1024 + cx + 16]
        if not any(colours):
            raise ValueError('missing/placeholder Force item palette')
        if colours[0]:
            raise ValueError('Force item index zero must be transparent on Saturn')
        if shade:
            colours = [sum(((c >> shift & 31) // 4) << shift for shift in (0, 5, 10))
                       | 0x8000 if c else 0 for c in colours]
        palettes.append(struct.pack('>16H', *(saturn_colour(c) for c in colours)))
        codes = [vram[(y + row) * 1024 + 512 + x // 4] >> (x % 4 * 4) & 15
                 for row in range(height) for x in range(64)]
        if not any(codes):
            raise ValueError('missing/placeholder Force item pixels')
        pixels.append(bytes(codes) if shade else
                      bytes(codes[i] << 4 | codes[i + 1] for i in range(0, len(codes), 2)))
    return b'T3FI0001' + struct.pack('>4H', 64, 128, 64, 64) + b''.join(palettes + pixels)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    for level in range(1, 5):
        image = build((args.raw / f'{50 + level:03}.bin').read_bytes())
        path = args.out / f'ITEM{level}.BIN'
        path.write_bytes(image)
        print(path, len(image))


if __name__ == '__main__':
    main()
