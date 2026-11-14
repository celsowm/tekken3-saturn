#!/usr/bin/env python3
"""Write PKEYS.BIN, the pictures of practice's key display, from the USA executable's system textures.

The key display (practice.ovl FUN_800B7C84, FUN_800B7354, FUN_800B721C; practice_sim.py of the pinned Tekken3Remake) draws,
along the bottom, the last inputs: a 19 x 31 direction arrow (one of nine, flipped from three pictures, in one of four
colours) or a 20 x 32 button icon (two sprites over each other, the first's picture picked by the CLUT row). The pictures are
in texture page 6 of the system VRAM; the coordinates are the overlay's (the arrow table at 0x800B0EDC, Japan Rev.1).

PKEYS.BIN (little endian): `T3PKEY1\\0`, u16 pieces; per piece u16 width, padded width, height, texel bytes, 16 u16 Saturn
colours (0 transparent) and LUT4 texels (left texel in the high nibble). Pieces: 36 arrows (index * 4 + colour, index 0-8 as
in the overlay) then 32 buttons (mask * 2 + guide, mask 1 LP, 2 RP, 4 LK, 8 RK). The output is retail-derived: keep it
under assets/imported or build/.

    scripts/build_practice_icons.py --exe assets/imported/practice-menu/SLUS_004.02 --title assets/imported/practice-menu/raw/010.bin \\
        --out assets/imported/practice-menu/PKEYS.BIN
"""
import argparse
import hashlib
import struct
import sys
from pathlib import Path

import extract_psx_hud as hud

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'third_party/Tekken3Remake/tools/research'))
import overlay_images  # noqa: E402

PAGE_X = 6 * 64                       # texture page 6 (x 384, y 0)
SCREEN_SLOT = 0x800B8D58              # where the USA release loads the screen overlays (title.ovl)
ICONS_RECT = (384, 224, 35, 32)       # the title archive's plain TIM that stays in VRAM for the fights: 140 x 32 texels
# The overlay's arrow table: per direction index (none, up, up-right, right, down-right, down, down-left, left, up-left)
# the picture's (v << 8 | u) and its flip flags (1 horizontal, 2 vertical).
ARROWS = ((0xE028, 0), (0xE050, 2), (0xE03C, 3), (0xE064, 1), (0xE03C, 1), (0xE050, 0), (0xE03C, 0), (0xE064, 0), (0xE03C, 2))
ARROW_CLUTS = (0x7FB2, 0x7FB3, 0x7FB4, 0x7FB5)      # colours 0-3
ARROW_W, ARROW_H = 19, 31
BUTTON_W, BUTTON_H = 20, 32


def with_icons(vram, title):
    """The system VRAM with the pad pictures the title screen uploads (title.ovl, FUN_8004CD28) kept for the fights."""
    for found in overlay_images.scan(title, SCREEN_SLOT):
        for tim in found.tims:
            flags = struct.unpack_from('<I', tim, 4)[0]
            if flags & 8:
                continue
            _, x, y, w, h = struct.unpack_from('<IHHHH', tim, 8)
            if (x, y, w, h) != ICONS_RECT:
                continue
            for row in range(h):
                dst = ((y + row) * 1024 + x) * 2
                vram[dst:dst + 2 * w] = tim[20 + row * 2 * w:20 + (row + 1) * 2 * w]
            return vram
    raise ValueError('the title overlay has no picture at ' + str(ICONS_RECT))


def palette(vram, clut):
    x, y = 16 * (clut & 63), (clut >> 6) & 511
    return [hud.word(vram, x + i, y) for i in range(16)]


def texel(vram, u, v):
    packed = hud.word(vram, PAGE_X + u // 4, v)
    return (packed >> (4 * (u & 3))) & 15


def region(vram, u0, v0, width, height, pal):
    """The rectangle as RGB555 words (0 for the transparent index)."""
    return [[pal[texel(vram, u0 + x, v0 + y)] if texel(vram, u0 + x, v0 + y) else 0 for x in range(width)] for y in range(height)]


def encode(name, rows):
    """A picture of RGB555 words as a LUT4 piece: its colours become the LUT (at most 15 beside transparent)."""
    counts = {}
    for row in rows:
        for value in row:
            if value:
                counts[value] = counts.get(value, 0) + 1
    colours = sorted(counts)
    remap = {}
    if len(colours) > 15:
        # LUT4 holds 15 colours beside transparent: the rarest are merged into their nearest kept neighbour.
        kept = sorted(colours, key=lambda value: -counts[value])[:15]
        for value in colours:
            if value not in kept:
                remap[value] = min(kept, key=lambda k: sum(((value >> s & 31) - (k >> s & 31)) ** 2 for s in (0, 5, 10)))
        colours = sorted(kept)
    rows = [[remap.get(value, value) for value in row] for row in rows]
    index = {value: i + 1 for i, value in enumerate(colours)}
    width, height = len(rows[0]), len(rows)
    padded = (width + 7) & ~7
    texels = bytearray()
    for row in rows:
        indices = [index.get(value, 0) for value in row] + [0] * (padded - width)
        texels += bytes((indices[i] << 4) | indices[i + 1] for i in range(0, padded, 2))
    lut = [0] + [(value & 0x7FFF) | 0x8000 for value in colours]
    lut += [0] * (16 - len(lut))
    return struct.pack('<4H', width, padded, height, len(texels)) + struct.pack('<16H', *lut) + bytes(texels)


def arrow(vram, index, colour):
    base, flags = ARROWS[index]
    rows = region(vram, base & 0xFF, base >> 8, ARROW_W, ARROW_H, palette(vram, ARROW_CLUTS[colour]))
    if flags & 1:
        rows = [row[::-1] for row in rows]
    if flags & 2:
        rows = rows[::-1]
    return encode(f'arrow {index}/{colour}', rows)


def button(vram, mask, guide):
    """FUN_800B721C: two sprites. The first (its CLUT row picks the pressed buttons: RK is the second row) is linked first,
    so the ordering table draws it last, over the second, guide-coloured plate."""
    symbols = palette(vram, 0x7F30 | ((mask & 8) << 3) | (mask & 7))
    plate = palette(vram, 0x7FB0 | (1 if guide else 0))
    over = region(vram, 0, 0xE0, BUTTON_W, BUTTON_H, symbols)
    under = region(vram, 0x14, 0xE0, BUTTON_W, BUTTON_H, plate)
    rows = [[over[y][x] or under[y][x] for x in range(BUTTON_W)] for y in range(BUTTON_H)]
    return encode(f'button {mask}/{guide}', rows)


def preview(blob, path, columns=12, scale=3):
    """A contact sheet of the pictures (for checking the extraction by eye)."""
    from PIL import Image
    count = struct.unpack_from('<H', blob, 8)[0]
    at, images = 10, []
    for _ in range(count):
        width, padded, height, size = struct.unpack_from('<4H', blob, at)
        lut = struct.unpack_from('<16H', blob, at + 8)
        data = blob[at + 40:at + 40 + size]
        image = Image.new('RGB', (width, height), (40, 40, 60))
        for y in range(height):
            for x in range(width):
                packed = data[y * padded // 2 + x // 2]
                value = lut[(packed >> 4) if x % 2 == 0 else (packed & 15)]
                if value:
                    image.putpixel((x, y), ((value & 31) << 3, ((value >> 5) & 31) << 3, ((value >> 10) & 31) << 3))
        images.append(image.resize((width * scale, height * scale), Image.NEAREST))
        at += 40 + size
    cell_w, cell_h = max(i.width for i in images) + 4, max(i.height for i in images) + 4
    sheet = Image.new('RGB', (columns * cell_w, ((len(images) + columns - 1) // columns) * cell_h), (0, 0, 0))
    for n, image in enumerate(images):
        sheet.paste(image, ((n % columns) * cell_w + 2, (n // columns) * cell_h + 2))
    sheet.save(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--exe', type=Path, required=True, help='the USA SLUS_004.02')
    parser.add_argument('--title', type=Path, required=True, help="the USA title.ovl record (BNS id 10, raw/010.bin)")
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--preview', type=Path, help='also write a PNG contact sheet of the pictures')
    args = parser.parse_args()
    from psx_disc_import import US_EXE_SHA
    exe = args.exe.read_bytes()
    if hashlib.sha256(exe).hexdigest() != US_EXE_SHA:
        raise ValueError('PKEYS.BIN needs the fingerprinted USA SLUS_004.02')
    vram = with_icons(hud.system_textures.build_vram(hud.system_textures.members(exe, 'usa')), args.title.read_bytes())
    pieces = [arrow(vram, i, c) for i in range(len(ARROWS)) for c in range(4)]
    pieces += [button(vram, m, g) for m in range(16) for g in (0, 1)]
    blob = b'T3PKEY1\0' + struct.pack('<H', len(pieces)) + b''.join(pieces)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(blob)
    print(f'{len(pieces)} pictures, {len(blob)} bytes -> {args.out}')
    if args.preview:
        preview(blob, args.preview)


if __name__ == '__main__':
    sys.exit(main())
