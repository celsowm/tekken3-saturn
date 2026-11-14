#!/usr/bin/env python3
"""Write PFONT.BIN, the original print_text glyphs (fonts 0 and 1) and message colours practice's text uses, from the USA executable.

Practice's HUD, prompts and menu pages print with FUN_8004D15C (hud.md#text-engine; draw_sim.py of the pinned Tekken3Remake):
font 0 (10 x 16 glyphs, advance 9, line 18) and font 1 (14 x 24, advance 13, line 26), through the message colours' CLUTs.
The glyphs are colour independent; a palette per colour picks the colour at draw time.

PFONT.BIN (little endian): `T3PFNT1\\0`, u16 glyphs, u16 palettes; the palettes first (u8 font, u8 colour, 16 u16 Saturn
colours), then per glyph u8 font, u8 character, u16 width, padded width, height, texel bytes and the LUT4 texels (left texel in
the high nibble). The output is retail-derived: keep it under assets/imported or build/.

    scripts/build_practice_font.py --exe assets/imported/practice-menu/SLUS_004.02 --out assets/imported/practice-menu/PFONT.BIN
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

import extract_psx_hud as hud

CHARS = ''.join(chr(c) for c in range(0x21, 0x5B))      # ! to Z: the digits, capital letters and punctuation
FONTS = (0, 1)
COLOURS = range(0, 16)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--exe', type=Path, required=True, help='the USA SLUS_004.02')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--preview', type=Path, help='also write a PNG of the glyphs in colour 1')
    args = parser.parse_args()
    from psx_disc_import import US_EXE_SHA
    exe = args.exe.read_bytes()
    if hashlib.sha256(exe).hexdigest() != US_EXE_SHA:
        raise ValueError('PFONT.BIN needs the fingerprinted USA SLUS_004.02')
    vram = hud.system_textures.build_vram(hud.system_textures.members(exe, 'usa'))
    palettes, glyphs, preview = [], [], []
    for font in FONTS:
        for colour in COLOURS:
            lut, _ = hud.extract_text_palette(vram, colour, font) if _visible(vram, colour, font) else (None, None)
            if lut is not None:
                palettes.append(struct.pack('<BB', font, colour) + lut)
        for char in CHARS:
            try:
                _, pixels, lut, info = hud.extract_glyph(vram, exe, char, 1, font)
            except ValueError:
                continue
            if not info['visible_pixels']:
                continue
            glyphs.append(struct.pack('<BBHHHH', font, ord(char), info['width'], info['padded_width'], info['height'], len(pixels)) + pixels)
            preview.append((font, char, info, pixels, lut))
    blob = b'T3PFNT1\0' + struct.pack('<HH', len(glyphs), len(palettes)) + b''.join(palettes) + b''.join(glyphs)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    print(f'{len(glyphs)} glyphs, {len(palettes)} palettes, {len(blob)} bytes -> {args.out}')
    if args.preview:
        write_preview(preview, args.preview)


def _visible(vram, colour, font):
    clut_x, clut_y = hud.text_clut_xy(colour, font)
    palette = [hud.word(vram, clut_x + i, clut_y) for i in range(16)]
    return palette[0] == 0 and any(palette)


def write_preview(glyphs, path):
    from PIL import Image
    rows = {}
    for font, char, info, pixels, lut in glyphs:
        rows.setdefault(font, []).append((char, info, pixels, lut))
    cell = 28
    sheet = Image.new('RGB', (cell * 30, cell * 2 * len(rows)), (0, 0, 40))
    for r, (font, items) in enumerate(sorted(rows.items())):
        for n, (char, info, pixels, lut) in enumerate(items):
            palette = struct.unpack('<16H', lut)
            for y in range(info['height']):
                for x in range(info['width']):
                    b = pixels[y * info['padded_width'] // 2 + x // 2]
                    value = palette[(b >> 4) if x % 2 == 0 else (b & 15)]
                    if value:
                        sheet.putpixel(((n % 30) * cell + x + 2, (r * 2 + n // 30) * cell + y + 2),
                                       ((value & 31) << 3, ((value >> 5) & 31) << 3, ((value >> 10) & 31) << 3))
    sheet = sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST)
    sheet.save(path)


if __name__ == '__main__':
    sys.exit(main())
