#!/usr/bin/env python3
"""Extract original fight HUD pieces from the local USA PS1 executable.

Writes private VDP1 LUT4 patterns and RGB555 palettes, with zero transparent.
The source decoder and page/CLUT coordinates are from pinned Tekken3Remake
(PolyForm Noncommercial 1.0.0; see NOTICE.md). No game image enters git.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'
sys.path.insert(0, str(RESEARCH))
import system_textures  # noqa: E402

# PS1 VRAM word positions and CLUT ids in hud.md and hud.py of the pinned remake.
PIECES = (
    ('bar_cap_left', 832, 0, 0xE0, 0xA8, 6, 24, 0x7EDE),
    ('bar_cap_right', 832, 0, 0xEA, 0xA8, 6, 24, 0x7EDE),
    ('bar_health', 832, 0, 0xE8, 0xA8, 2, 24, 0x7EDC),
    ('bar_damage', 832, 0, 0xE8, 0xA8, 2, 24, 0x7EDD),
    ('bar_empty', 832, 0, 0xE8, 0xA8, 2, 24, 0x7EDE),
    ('mark_won', 832, 0, 0xF0, 0xA8, 15, 18, 0x7F18),
    ('mark_empty', 832, 0, 0xF0, 0xA8, 15, 18, 0x7F19),
    *((f'digit_{d}', 896, 0, 16 * d, 0xD0, 16, 46, 0x7F1C) for d in range(12)),
    # FUN_8004EB9C's plate backing: a 2 x 10 quad stretched over the name.
    ('plate_0', 832, 0, 0xDC, 0xA8, 2, 10, 0x7F11),
)

# FUN_8004EB9C's name sprites: page 2 (word 448) of each fighter's costume
# texture ARC, U 0x40, 16 rows, through the system CLUT 0x7F10 (costume 0).
# The widths are the character records' byte +5 (COSTUME_KEYS 0x80095CE8 key
# 0 for Paul and key 4 for Law, CHAR_RECORDS 0x80098120), read through the
# pinned remake's canonical-rebased USA disc; the extracted pixels are
# byte-identical to its verified hud.py converter. P1's name is left-anchored
# at x 14 and P2's right-anchored at 354, so each pads to the 8-texel VDP1
# width on its outer side and the inner art stays flush with its anchor.
NAME_CLUT = 0x7F10
NAME_PAGE_WORD = 448
NAME_U = 0x40
NAME_HEIGHT = 16
NAMES = (
    ('name_paul', 'actor0_arc', 32, 'left'),
    ('name_law', 'actor1_arc', 26, 'right'),
)


def word(vram, x, y):
    if not 0 <= x < 1024 or not 0 <= y < 512:
        raise ValueError(f'VRAM coordinate outside 1024x512: {x},{y}')
    return struct.unpack_from('<H', vram, 2 * (1024 * y + x))[0]


def extract(vram, spec):
    name, page_x, page_y, u, v, width, height, clut = spec
    clut_x, clut_y = 16 * (clut & 63), (clut >> 6) & 511
    palette = [word(vram, clut_x + i, clut_y) for i in range(16)]
    if palette[0] != 0:
        raise ValueError(f'{name}: transparent palette entry is not zero')
    padded_width = (width + 7) & ~7
    pixels = bytearray()
    visible = 0
    for y in range(v, v + height):
        indices = []
        for x in range(u, u + width):
            packed = word(vram, page_x + x // 4, page_y + y)
            index = (packed >> (4 * (x & 3))) & 15
            visible += palette[index] != 0
            indices.append(index)
        # VDP1 requires an 8-texel width. Resample across that width so a
        # stretched 2-texel fill remains visible for the full destination.
        if padded_width != width:
            indices = [indices[col * width // padded_width] for col in range(padded_width)]
        pixels += bytes((indices[i] << 4) | indices[i + 1]
                        for i in range(0, padded_width, 2))
    # Saturn VDP1 LUT colours need the RGB bit set to mix with indexed sprites.
    lut = b''.join(struct.pack('<H', (value & 0x7FFF) | 0x8000 if value else 0)
                   for value in palette)
    return name, bytes(pixels), lut, dict(width=width, padded_width=padded_width,
                                          height=height, clut=clut, visible_pixels=visible)


def extract_name(char_vram, system_vram, spec):
    """One fighter's name sprite: page 2 of the costume ARC, zero-padded
    towards the sprite's outer side so the art stays flush with its anchor
    when the padded sheet is drawn scaled (FUN_8004EB9C, hud.py)."""
    name, _, width, align = spec
    clut_x, clut_y = 16 * (NAME_CLUT & 63), (NAME_CLUT >> 6) & 511
    palette = [word(system_vram, clut_x + i, clut_y) for i in range(16)]
    if palette[0] != 0:
        raise ValueError(f'{name}: transparent palette entry is not zero')
    padded_width = (width + 7) & ~7
    pixels = bytearray()
    visible = 0
    for y in range(NAME_HEIGHT):
        indices = []
        for x in range(width):
            packed = char_vram[y * 1024 + NAME_PAGE_WORD + (NAME_U + x) // 4]
            index = (packed >> (4 * ((NAME_U + x) & 3))) & 15
            visible += palette[index] != 0
            indices.append(index)
        pad = [0] * (padded_width - width)
        indices = pad + indices if align == 'right' else indices + pad
        pixels += bytes((indices[i] << 4) | indices[i + 1]
                        for i in range(0, padded_width, 2))
    lut = b''.join(struct.pack('<H', (value & 0x7FFF) | 0x8000 if value else 0)
                   for value in palette)
    return name, bytes(pixels), lut, dict(width=width, padded_width=padded_width,
                                         height=NAME_HEIGHT, clut=NAME_CLUT,
                                         visible_pixels=visible, align=align)


# The text engine (hud.md#text-engine, FUN_8004D15C ported as print_text):
# font 2's 24 x 40 glyphs, advance 22, on tpage 0x2E (VRAM (896, 0); the
# verified hud.py converter resolves tpage 0x2E there), through the message
# colours' CLUTs at x = 256 + 16*((colour + 32) & 15),
# y = ((colour + 32) >> 4) + 24 | 480 (print_text's %c). The USA font table is
# 0x80021D64 (Japan Rev.1 0x80021FD0 - 0x26C, located by scanning the USA
# executable for the font descriptors and verified against the pinned remake's
# canonical-rebased disc): font 2's descriptor is at +0x20 and its glyph UV
# table at 0x80021CA4 (u = low byte, v = high byte, per character code).
FONT2_UV_TABLE = 0x80021CA4
FONT2_PAGE = (896, 0)
FONT2_WIDTH, FONT2_HEIGHT, FONT2_ADVANCE = 24, 40, 22
# The fixed round messages' colours (hud.md#round-messages); the palette
# entries follow this order in HUD.BIN and on the Saturn.
TEXT_COLOURS = (1, 2, 3, 4, 5, 6, 8, 9)
GLYPH_FILE_NAMES = {'!': 'excl', '.': 'dot', '?': 'query'}
MESSAGE_CHARS = 'ABCDEFIKLMNOPRSTUWY!.?123456789'
REPLAY_CHARS = 'REPLAY'
FONT_TABLE = 0x80021D64


def text_clut_xy(colour, font=2):
    k = colour + (16 if font == 1 else 32)
    return 256 + 16 * (k & 15), ((k >> 4) + 24) | 480


def extract_glyph(vram, exe, char, colour, font=2):
    """Original font 1/2 glyph, padded to an eight-texel VDP1 width.

    Texture indices are stored once; message colour overrides the CLUT at
    draw time. Metrics and UV pointers come from the fingerprinted USA EXE.
    """
    table, advance, line, width, height, page_id, offset, _ = struct.unpack_from(
        '<I5H2B', exe, FONT_TABLE - 0x80010000 + 0x800 + 16 * font)
    expected = (13, 26, 14, 24, 0x2D, 16) if font == 1 else (22, 42, 24, 40, 0x2E, 32)
    if (advance, line, width, height, page_id, offset) != expected:
        raise ValueError(f'font {font}: unexpected USA descriptor')
    page_x = (page_id & 15) * 64
    page_y = ((page_id >> 4) & 1) * 256
    padded_width = (width + 7) & ~7
    uv, = struct.unpack_from('<H', exe, table - 0x80010000 + 0x800 + 2 * ord(char))
    u0, v0 = uv & 0xFF, uv >> 8
    clut_x, clut_y = text_clut_xy(colour, font)
    palette = [word(vram, clut_x + i, clut_y) for i in range(16)]
    if palette[0] != 0:
        raise ValueError(f'glyph {char!r}: transparent palette entry is not zero')
    pixels = bytearray()
    visible = 0
    for y in range(height):
        indices = []
        for x in range(width):
            packed = word(vram, page_x + (u0 + x) // 4, page_y + v0 + y)
            index = (packed >> (4 * ((u0 + x) & 3))) & 15
            visible += palette[index] != 0
            indices.append(index)
        indices += [0] * (padded_width - width)
        pixels += bytes((indices[i] << 4) | indices[i + 1]
                        for i in range(0, padded_width, 2))
    lut = b''.join(struct.pack('<H', (value & 0x7FFF) | 0x8000 if value else 0)
                   for value in palette)
    name = ('replay_' if font == 1 else 'glyph_') + GLYPH_FILE_NAMES.get(char, char)
    info = dict(width=width, padded_width=padded_width, height=height, font=font,
                visible_pixels=visible, char=char, uv=[u0, v0],
                palette_xy=[clut_x, clut_y])
    return name, bytes(pixels), lut, info


def extract_text_palette(vram, colour, font=2):
    """One message colour's 16-entry RGB555 CLUT as a 32-byte LUT blob."""
    clut_x, clut_y = text_clut_xy(colour, font)
    palette = [word(vram, clut_x + i, clut_y) for i in range(16)]
    if palette[0] != 0:
        raise ValueError(f'text colour {colour}: transparent palette entry is not zero')
    if not any(palette):
        raise ValueError(f'text colour {colour}: no visible entries')
    return b''.join(struct.pack('<H', (value & 0x7FFF) | 0x8000 if value else 0)
                    for value in palette), dict(colour=colour, palette_xy=[clut_x, clut_y])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/imported/tekken3-usa/hud')
    parser.add_argument('--actor0-arc', type=Path, required=True,
                        help="player 1's costume texture ARC (the shared character archive)")
    parser.add_argument('--actor1-arc', type=Path, required=True,
                        help="player 2's costume texture ARC")
    args = parser.parse_args()
    sys.path.insert(0, str(ROOT / 'scripts'))
    from psx_character_textures import load_character_vram
    from psx_disc_import import US_EXE_SHA
    exe = args.exe.read_bytes()
    if hashlib.sha256(exe).hexdigest() != US_EXE_SHA:
        raise ValueError('HUD extraction requires the fingerprinted USA SLUS_004.02')
    tims = system_textures.members(exe, 'usa')
    if len(tims) != 127:
        raise ValueError(f'expected 127 USA system texture members, got {len(tims)}')
    vram = system_textures.build_vram(tims)
    images = {}
    packed = bytearray(b'T3HUD04\0' + struct.pack('<HH', len(PIECES) + len(NAMES) + len(MESSAGE_CHARS) + len(REPLAY_CHARS),
                                                  len(TEXT_COLOURS) + 1))
    args.out.mkdir(parents=True, exist_ok=True)
    for spec in PIECES + NAMES:
        if spec in NAMES:
            arc_path = getattr(args, spec[1].replace('-', '_'))
            arc = arc_path.read_bytes()
            char_vram, _ = load_character_vram(arc)
            name, pixels, lut, info = extract_name(char_vram, vram, spec)
            info = dict(info, arc_sha256=hashlib.sha256(arc).hexdigest())
        else:
            name, pixels, lut, info = extract(vram, spec)
        if not info['visible_pixels']:
            raise ValueError(f'{name} has no visible texels')
        filename, palette_file = name + '.lut4', name + '.clut555'
        (args.out / filename).write_bytes(pixels)
        (args.out / palette_file).write_bytes(lut)
        # Retire the first extractor's per-pixel RGB output by exact name.
        (args.out / (name + '.rgb555')).unlink(missing_ok=True)
        images[name] = dict(file=filename, palette_file=palette_file,
                            sha256=hashlib.sha256(pixels).hexdigest(),
                            palette_sha256=hashlib.sha256(lut).hexdigest(), **info)
        packed += struct.pack('<HHHH', info['width'], info['padded_width'], info['height'], len(pixels))
        packed += lut + pixels
    for font, char in [(2, c) for c in MESSAGE_CHARS] + [(1, c) for c in REPLAY_CHARS]:
        name, pixels, lut, info = extract_glyph(vram, exe, char, 7 if font == 1 else 8, font)
        if not info['visible_pixels']:
            raise ValueError(f'{name} has no visible texels')
        filename, palette_file = name + '.lut4', name + '.clut555'
        (args.out / filename).write_bytes(pixels)
        (args.out / palette_file).write_bytes(lut)
        images[name] = dict(file=filename, palette_file=palette_file,
                            sha256=hashlib.sha256(pixels).hexdigest(),
                            palette_sha256=hashlib.sha256(lut).hexdigest(), **info)
        packed += struct.pack('<HHHH', info['width'], info['padded_width'], info['height'], len(pixels))
        packed += lut + pixels
    text_palettes = {}
    for font, colour in [(2, c) for c in TEXT_COLOURS] + [(1, 7)]:
        lut, info = extract_text_palette(vram, colour, font)
        palette_file = f'text_font_{font}_colour_{colour}.clut555'
        (args.out / palette_file).write_bytes(lut)
        text_palettes[f'{font}:{colour}'] = dict(palette_file=palette_file,
                                          palette_sha256=hashlib.sha256(lut).hexdigest(), **info)
        packed += lut
    (args.out / 'HUD.BIN').write_bytes(packed)
    manifest = dict(schema=4, source='Tekken 3 USA system textures and costume texture archives',
                    exe_sha256=hashlib.sha256(exe).hexdigest(),
                    format='VDP1 LUT4, left texel in high nibble; RGB555 LE CLUT with bit15 visible',
                    disc_file='HUD.BIN', disc_sha256=hashlib.sha256(packed).hexdigest(),
                    text=dict(font=2, width=FONT2_WIDTH, height=FONT2_HEIGHT,
                              advance=FONT2_ADVANCE, uv_table=f'{FONT2_UV_TABLE:#x}',
                              page=list(FONT2_PAGE), chars=MESSAGE_CHARS,
                              colours=list(TEXT_COLOURS)),
                    replay_text=dict(font=1, width=14, padded_width=16, height=24,
                                     advance=13, chars=REPLAY_CHARS, colour=7),
                    text_palettes=text_palettes,
                    images=images)
    (args.out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'{len(images)} HUD images + {len(TEXT_COLOURS) + 1} text palettes -> {args.out}')


if __name__ == '__main__':
    main()
