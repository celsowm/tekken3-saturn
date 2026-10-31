#!/usr/bin/env python3
"""Extract the character select faces from the local USA PS1 executable.

The HUD portraits of character ids 0-19 (system texture members 9 + id, 32 x 58,
8-bit, own CLUT row 480 + id; third_party/Tekken3Remake docs/research/formats/
system-textures.md) are quantised to 15 colours plus transparent, so each is a
VDP1 LUT4 pattern, with a second darkened, desaturated palette for cells that
cannot be chosen. Source layout from the pinned Tekken3Remake (PolyForm
Noncommercial 1.0.0; see NOTICE.md). The output is private: no game image
enters git.

SELECT.BIN: 'T3SEL01\\0', u16 count, u16 width, u16 height, u16 reserved, then
per face: 16 u16 LUT (VDP1 RGB555 with bit 15 set, 0 transparent), the same
ghosted, and width * height / 2 packed 4-bit pixels.
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
import system_textures  # noqa: E402

FACES, WIDTH, HEIGHT = 20, 32, 58
GHOST_LEVEL = 0.42


def rgb555(value):
    return ((value & 31) << 3 | (value & 31) >> 2, (value >> 5 & 31) << 3 | (value >> 5 & 31) >> 2,
            (value >> 10 & 31) << 3 | (value >> 10 & 31) >> 2)


def pack555(r, g, b):
    return (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10


def decode(tims, ident):
    """The portrait's 32 x 58 palette indices and RGB555 CLUT from its own TIM."""
    blocks = {b[0]: b for b in system_textures.tim_blocks(tims[9 + ident][1])}
    palette = list(struct.unpack_from('<256H', blocks['clut'][5]))
    _, _, _, words, rows, data = blocks['image']
    if (words, rows) != (WIDTH // 2, HEIGHT):
        raise ValueError(f'portrait {ident}: unexpected {words} x {rows} words')
    return np.frombuffer(data, np.uint8).reshape(HEIGHT, WIDTH).copy(), palette


def quantise(pixels, palette):
    rgb = np.zeros((HEIGHT, WIDTH, 3), np.uint8)
    opaque = np.zeros((HEIGHT, WIDTH), bool)
    for y in range(HEIGHT):
        for x in range(WIDTH):
            value = palette[pixels[y, x]]
            opaque[y, x] = value != 0
            rgb[y, x] = rgb555(value)
    image = Image.fromarray(rgb).quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    table = (image.getpalette() + [0] * 45)[:45]
    indices = np.asarray(image, np.uint8) + 1
    indices[~opaque] = 0
    lut = [0] + [pack555(*table[3 * i:3 * i + 3]) | 0x8000 for i in range(15)]
    ghost = [0] + [pack555(*(int(sum(table[3 * i:3 * i + 3]) / 3 * GHOST_LEVEL),) * 3) | 0x8000
                   for i in range(15)]
    return indices, lut, ghost


def build(exe):
    tims = system_textures.members(exe, 'usa')
    out = bytearray(b'T3SEL01\0' + struct.pack('<HHHH', FACES, WIDTH, HEIGHT, 0))
    sheet = Image.new('RGB', (FACES * WIDTH, HEIGHT * 2))
    for ident in range(FACES):
        pixels, palette = decode(tims, ident)
        indices, lut, ghost = quantise(pixels, palette)
        out += struct.pack('<16H', *lut) + struct.pack('<16H', *ghost)
        flat = indices.flatten()
        out += bytes(int(flat[i]) << 4 | int(flat[i + 1]) for i in range(0, len(flat), 2))
        for row, table in enumerate((lut, ghost)):
            for y in range(HEIGHT):
                for x in range(WIDTH):
                    index = indices[y, x]
                    sheet.putpixel((ident * WIDTH + x, row * HEIGHT + y),
                                   rgb555(table[index] & 0x7FFF) if index else (255, 0, 255))
    return bytes(out), sheet


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    parser.add_argument('--out', type=Path, required=True, help='SELECT.BIN path')
    parser.add_argument('--sheet', type=Path, help='optional private PNG contact sheet')
    args = parser.parse_args()
    from psx_disc_import import US_EXE_SHA
    exe = args.exe.read_bytes()
    if hashlib.sha256(exe).hexdigest() != US_EXE_SHA:
        raise ValueError('select art extraction requires the fingerprinted USA SLUS_004.02')
    data, sheet = build(exe)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(data)
    if args.sheet:
        sheet.resize((sheet.width * 3, sheet.height * 3), Image.Resampling.NEAREST).save(args.sheet)
    print(len(data))


if __name__ == '__main__':
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    main()
