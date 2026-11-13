#!/usr/bin/env python3
"""Write FHUD.BIN, the original pieces the Tekken Force fight HUD draws, from the USA executable.

Bar caps and fills, the name plate and the timer digits are FUN_8004E55C's, FUN_8004EB9C's and
FUN_8004E410's (hud.md). The glyphs are print_text's (FUN_8004D15C) for force.ovl's texts: the mode
line (ModeCaption mode 10, font 0), LIFE UP!, "+n SEC", NOW LOADING... and the bonus time left
(fonts 0 and 1), the stage clear tally (font 1) and STAGE CLEAR!! (font 2), with the colours they
use. The four Force enemies' names (CROW, FALCON, HAWK, OWL) follow, right-aligned: each enemy's
costume ARC keeps all four on its name page, enemy k at V = 16 k (hud.py, force.ovl FUN_800B362C).
The decoders and coordinates are extract_psx_hud.py's, from the pinned Tekken3Remake (PolyForm
Noncommercial 1.0.0; see NOTICE.md).

FHUD.BIN: `T3FHUD2\\0`, u16 piece count, u16 palette count (little-endian); per piece u16 width,
padded width, height, texel bytes, u8 font and character (0xFF, 0 for the HUD pieces; 0xFE and k
for enemy k's name), u16 0, a
16-colour LUT and LUT4 texels; then per palette u8 font, u8 colour, u16 0 and the 32-byte table.
A glyph's texels are colour-independent; the palette picks the colour. The output is
retail-derived: keep it under assets/imported or build/.
"""
import argparse
import hashlib
import struct
from pathlib import Path

import extract_psx_hud as hud
from fighter_roster import FORCE_ENEMIES, records
from psx_character_textures import load_character_vram

# Order shared with src/saturn/force_hud.c.
BAR = ('bar_cap_left', 'bar_cap_right', 'bar_health', 'bar_damage', 'bar_empty', 'plate_0')
DIGITS = tuple(f'digit_{d}' for d in range(10))
# Per font: the texts force_hud.c prints (numbers as their digits) and the colours they use.
FONT_TEXTS = {
    0: ('SCORE', 'HI-SCORE', '0123456789', 'LIFE UP!', '+', 'SEC', 'NOW LOADING...', '"'),
    1: ('STAGE BONUS : ', 'CLEAR BONUS : ', 'CLEAR TIME : ', 'BONUS TIME : ', '0123456789\'"',
        'YOU GOT A KEY!!', 'CHALLENGE FINAL STAGE!!'),
    2: ('STAGE CLEAR!!',),
}
FONT_CHARS = {font: ''.join(sorted(set(''.join(texts)) - {' '})) for font, texts in FONT_TEXTS.items()}
FONT_COLOURS = {0: (2, 5, 6), 1: (1, 2, 4, 5, 6), 2: (8,)}
ENEMY_NAME = 0xFE


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--exe', type=Path, required=True, help='the USA SLUS_004.02')
    parser.add_argument('--raw', type=Path, required=True, help='private directory of NNN.bin records')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    from psx_disc_import import US_EXE_SHA
    exe = args.exe.read_bytes()
    if hashlib.sha256(exe).hexdigest() != US_EXE_SHA:
        raise ValueError('FHUD.BIN needs the fingerprinted USA SLUS_004.02')
    vram = hud.system_textures.build_vram(hud.system_textures.members(exe, 'usa'))
    specs = {spec[0]: spec for spec in hud.PIECES}
    pieces = [(0xFF, 0, hud.extract(vram, specs[name])) for name in BAR + DIGITS]
    pieces += [(font, ord(char), hud.extract_glyph(vram, exe, char, FONT_COLOURS[font][0], font))
               for font, chars in FONT_CHARS.items() for char in chars]
    for k, enemy in enumerate(FORCE_ENEMIES):
        char_vram, _ = load_character_vram((args.raw / f"{records(enemy)['arc']:03}.bin").read_bytes(), 0)
        spec = (enemy.name.lower(), None, enemy.name_width, 'right')
        pieces.append((ENEMY_NAME, k, hud.extract_name(char_vram, vram, spec, 16 * k)))
    palettes = [(font, colour) for font, colours in FONT_COLOURS.items() for colour in colours]
    out = bytearray(b'T3FHUD2\0' + struct.pack('<HH', len(pieces), len(palettes)))
    for font, char, (name, pixels, lut, info) in pieces:
        if not info['visible_pixels']:
            raise ValueError(f'{name} (font {font}) has no visible texels')
        out += struct.pack('<HHHHBBH', info['width'], info['padded_width'], info['height'], len(pixels),
                           font, char, 0) + lut + pixels
    for font, colour in palettes:
        out += struct.pack('<BBH', font, colour, 0) + hud.extract_text_palette(vram, colour, font)[0]
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(out)
    print(f'{len(pieces)} pieces, {len(palettes)} palettes, {len(out)} bytes -> {args.out}')


if __name__ == '__main__':
    main()
