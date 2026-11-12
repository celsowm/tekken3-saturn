#!/usr/bin/env python3
"""Draw a Tekken Force level's tile panorama as the PS1 does, to a PNG.

force.ovl FUN_800B4DF8 draws the level's tile map as 64 x 64 sprites over the black clear colour
(Tekken3Remake docs/research/code/modes.md, Panorama; stages.md, Clear colour). A texel whose CLUT
entry is 0x0000 is not drawn, so the clear shows through it. With --mark those texels are magenta
instead of black, to tell them apart from opaque black.

The output is retail-derived: keep it under assets/imported, build/ or a scratch directory.
"""
import argparse
from pathlib import Path
import struct
import zlib

from build_force_levels import FIRST_RECORD, TILE, read_stage, tile_texels


def rgb(colour):
    """PS1 15-bit colour to 8-bit RGB."""
    return tuple((colour >> s & 31) * 255 // 31 for s in (0, 5, 10))


def write_png(path, width, height, pixels):
    raw = b''.join(b'\0' + bytes(c for p in pixels[y * width:(y + 1) * width] for c in p) for y in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def render(arc, mark):
    """(width, height, pixels, transparent texel count) of the whole map."""
    members, vram = read_stage(arc)
    width, height = struct.unpack_from('<HH', members[2])
    cells = width * height
    textures = struct.unpack_from(f'<{cells}H', members[2], 4)
    cluts = struct.unpack_from(f'<{cells}H', members[2], 4 + 2 * cells)
    hole = (255, 0, 255) if mark else (0, 0, 0)
    pixels, holes = [hole] * (width * TILE * height * TILE), 0
    for cell, (word, clut) in enumerate(zip(textures, cluts)):
        cx, cy = 16 * (clut & 63), clut >> 6
        colours = [vram[cy * 1024 + cx + i] for i in range(16)]
        x0, y0 = cell % width * TILE, cell // width * TILE
        for y, row in enumerate(tile_texels(vram, word)):
            for x, index in enumerate(row):
                if colours[index] == 0:
                    holes += 1
                else:
                    pixels[(y0 + y) * width * TILE + x0 + x] = rgb(colours[index])
    return width * TILE, height * TILE, pixels, holes


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--raw', type=Path, required=True, help='private directory of NNN.bin records')
    p.add_argument('--level', type=int, choices=range(1, 5), default=1)
    p.add_argument('--mark', action='store_true', help='draw undrawn texels magenta')
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    w, h, pixels, holes = render((a.raw / f'{FIRST_RECORD + a.level - 1:03}.bin').read_bytes(), a.mark)
    write_png(a.out, w, h, pixels)
    print(f'level {a.level}: {w} x {h}, {holes} undrawn texels')


if __name__ == '__main__':
    main()
