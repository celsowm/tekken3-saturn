#!/usr/bin/env python3
"""Write BHUD.BIN, the original art of Tekken Ball's HUD mode line and power gauges, from the USA executable.

The mode line (FUN_8004EFF8 / FUN_8004EE3C, hud.md; draw_sim.py hud_mode_line, volley.ovl FUN_800B5210)
puts the human side's `TEKKEN BALL` (font 0, colour 6) at y 24 and, for a side without a human, the
blinking coin prompt (FUN_8004F1D0: INSERT COIN or PUSH P2 START, every other 64 frames FREE PLAY or
CREDIT 0; font 0, colour 5). Each text is composed here from print_text's font-0 glyphs, an advance
apart, into one LUT4 sheet in its colour. The power gauges (volley.ovl FUN_800B4E6C, draw_sim.py
ball_gauges) are the health bar's caps (CLUT 0x7EDE) and its 2-texel strip, drawn with CLUT 0x7EDE
(player 1's fill), 0x7EDD (player 2's), 0x7EDC (the trail) or 0x7EDF (empty): the strip is stored once
per CLUT. The decoders and palettes are extract_psx_hud.py's, from the pinned Tekken3Remake (PolyForm
Noncommercial 1.0.0; see NOTICE.md).

BHUD.BIN: `T3BHUD01`, u16 sheet count (little-endian); per sheet u16 width, padded width, height, texel
bytes, then the 16-colour LUT (32 bytes) and LUT4 texels (left pixel in the high nibble). The sheet order
is src/saturn/ball_hud_art.c's. The output is retail-derived: keep it under build/.
"""
import argparse
import hashlib
import struct
from pathlib import Path

import extract_psx_hud as hud

# (text, colour); the order is ball_hud_art.c's BHUD_* enum, then the gauge sheets.
LABELS = (('TEKKEN BALL', 6), ('INSERT COIN', 5), ('PUSH P2 START', 5), (' FREE PLAY', 5), (' CREDIT 0', 5))
GAUGE_STRIP_CLUTS = (0x7EDC, 0x7EDD, 0x7EDE, 0x7EDF)
FONT, ADVANCE = 0, 9


def compose(glyphs, text):
    """The glyphs of `text` an advance apart (spaces advance the pen); a later glyph's opaque texels win."""
    width, height = glyphs[next(c for c in text if c != ' ')][1]['width'], 16
    used = ADVANCE * (len(text) - 1) + width
    padded = (used + 7) & ~7
    rows = [[0] * padded for _ in range(height)]
    for k, char in enumerate(text):
        if char == ' ':
            continue
        pixels, info = glyphs[char]
        pitch = info['padded_width']
        for y in range(height):
            for x in range(info['width']):
                b = pixels[y * pitch // 2 + x // 2]
                nib = b & 15 if x & 1 else b >> 4
                if nib:
                    rows[y][ADVANCE * k + x] = nib
    texels = bytes((row[i] << 4) | row[i + 1] for row in rows for i in range(0, padded, 2))
    return used, padded, height, texels


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--exe', type=Path, required=True, help='the USA SLUS_004.02')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    from psx_disc_import import US_EXE_SHA
    exe = args.exe.read_bytes()
    if hashlib.sha256(exe).hexdigest() != US_EXE_SHA:
        raise ValueError('BHUD.BIN needs the fingerprinted USA SLUS_004.02')
    vram = hud.system_textures.build_vram(hud.system_textures.members(exe, 'usa'))
    glyphs = {}
    for char in sorted(set(''.join(text for text, _ in LABELS)) - {' '}):
        _, pixels, _, info = hud.extract_glyph(vram, exe, char, 6, FONT)
        if info['height'] != 16 or not info['visible_pixels']:
            raise ValueError(f'font 0 glyph {char!r}: unexpected metrics')
        glyphs[char] = (pixels, info)
    sheets = []
    for text, colour in LABELS:
        used, padded, height, texels = compose(glyphs, text)
        sheets.append((used, padded, height, hud.extract_text_palette(vram, colour, FONT)[0], texels))
    specs = {spec[0]: spec for spec in hud.PIECES}
    gauge = [specs['bar_cap_left'], specs['bar_cap_right']]
    gauge += [specs['bar_empty'][:-1] + (clut,) for clut in GAUGE_STRIP_CLUTS]
    for spec in gauge:
        _, pixels, lut, info = hud.extract(vram, spec)
        sheets.append((info['width'], info['padded_width'], info['height'], lut, pixels))
    out = bytearray(b'T3BHUD01' + struct.pack('<H', len(sheets)))
    for width, padded, height, lut, texels in sheets:
        if len(lut) != 32 or len(texels) != padded * height // 2:
            raise ValueError('unexpected sheet layout')
        out += struct.pack('<HHHH', width, padded, height, len(texels)) + lut + texels
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f'{len(sheets)} sheets, {len(out)} bytes -> {args.out}')


if __name__ == '__main__':
    main()
