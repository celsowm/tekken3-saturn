#!/usr/bin/env python3
"""Write FHUD.BIN, the original pieces the Tekken Force fight HUD draws, from the USA executable.

Bar caps and fills, the name plate and the timer digits are FUN_8004E55C's, FUN_8004EB9C's and
FUN_8004E410's (hud.md); the mode line's font-0 glyphs and colours are ModeCaption's (FUN_8004EE3C:
`SCORE` colour 5 and `HI-SCORE` colour 2, the numbers colour 6). The decoders and coordinates are
extract_psx_hud.py's, from the pinned Tekken3Remake (PolyForm Noncommercial 1.0.0; see NOTICE.md).

FHUD.BIN: `T3FHUD1\\0`, u16 piece count, u16 palette count (little-endian); per piece u16 width,
padded width, height, texel bytes, a 16-colour LUT and LUT4 texels; then the text palettes. The
output is retail-derived: keep it under assets/imported or build/.
"""
import argparse
import hashlib
import struct
from pathlib import Path

import extract_psx_hud as hud

# Order shared with src/saturn/force_hud.c.
BAR = ('bar_cap_left', 'bar_cap_right', 'bar_health', 'bar_damage', 'bar_empty', 'plate_0')
DIGITS = tuple(f'digit_{d}' for d in range(10))
MODE_CHARS = 'SCOREHI-0123456789'
MODE_COLOURS = (5, 2, 6)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--exe', type=Path, required=True, help='the USA SLUS_004.02')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    from psx_disc_import import US_EXE_SHA
    exe = args.exe.read_bytes()
    if hashlib.sha256(exe).hexdigest() != US_EXE_SHA:
        raise ValueError('FHUD.BIN needs the fingerprinted USA SLUS_004.02')
    vram = hud.system_textures.build_vram(hud.system_textures.members(exe, 'usa'))
    specs = {spec[0]: spec for spec in hud.PIECES}
    pieces = [hud.extract(vram, specs[name]) for name in BAR + DIGITS]
    pieces += [hud.extract_glyph(vram, exe, char, MODE_COLOURS[0], 0) for char in MODE_CHARS]
    out = bytearray(b'T3FHUD1\0' + struct.pack('<HH', len(pieces), len(MODE_COLOURS)))
    for name, pixels, lut, info in pieces:
        if not info['visible_pixels']:
            raise ValueError(f'{name} has no visible texels')
        out += struct.pack('<HHHH', info['width'], info['padded_width'], info['height'], len(pixels)) + lut + pixels
    for colour in MODE_COLOURS:
        out += hud.extract_text_palette(vram, colour, 0)[0]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f'{len(pieces)} pieces, {len(MODE_COLOURS)} palettes, {len(out)} bytes -> {args.out}')


if __name__ == '__main__':
    main()
