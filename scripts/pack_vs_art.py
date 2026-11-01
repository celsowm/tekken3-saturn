#!/usr/bin/env python3
"""Pack the pre-fight VS screen from the local USA disc.

Inputs (private, from scripts/psx_disc_import.py --extract-ids 5,12-35 and
--dump-exe): arcade.ovl (record 5), makuma00/01.tia (12, 13), face_b00..21.tiz
(14..35), SLUS_004.02. The rules follow Tekken3Remake (n0madic, PolyForm
Noncommercial 1.0.0; see NOTICE.md), docs/research/code/modes.md and
tools/research/draw_sim.py. Output is private and never enters git.

The screen is one 352x448 VDP2 bitmap at 2x horizontal zoom, composed on the
Saturn from these parts:

  VSBG0..3.BIN   'T3BG0001' as in pack_menu_art.py: background, caption bar and VS.
  VSPANEL.BIN    per background picture, side and fighter: 80 RGB555 words, then
                 PANEL_W*PANEL_H codes (VS_PANEL_BYTES each). The portrait, frame and
                 additive tint are baked over the background, because about 40% of a
                 portrait is transparent and the tint lights what shows through.
  VSTEXT.BIN     VS_TEXTS records of u8 width, u8 height, then VS_TEXT_W*VS_TEXT_H codes,
                 0 transparent: fighter names, then STAGE, FIGHT, PRACTICE MODE and digits.
  vs_layout.h    the positions, in bitmap dots.

Codes: 0..94 background and text, 95 the credit line, 96..175 side 0, 176..255 side 1.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
sys.path.insert(0, str(ROOT / 'scripts'))
from export_compressed_tim import parse_indexed_tim  # noqa: E402
from inspect_compressed_tim import decompress_tim, tia_members  # noqa: E402
import pack_menu_art as menu  # noqa: E402

SOURCE_W, SOURCE_H = 368, 480
W, H = menu.W, menu.H
FIGHTERS = 21
PORTRAIT_W, PORTRAIT_H, PORTRAIT_TOP = 126, 212, 20
FRAME_W, FRAME_H = 130, 224
PANEL_W, PANEL_H = FRAME_W * W // SOURCE_W + 1 & ~1, FRAME_H * H // SOURCE_H
BACKGROUND_COLOURS, GLYPH_COLOURS, PANEL_COLOURS = 50, 15, 80
CREDIT_CODE, CREDIT_WORD = 95, menu.word(150, 150, 160)
SIDE_CODES = (96, 176)
NAME_CODE, CAPTION_CODE, VS_CODE = (BACKGROUND_COLOURS + GLYPH_COLOURS * i for i in range(3))
# hud.md: a text's CLUT is its colour plus its font's offset (fonts 1, 2 and 3: 16, 32, 32).
NAME_CLUT, CAPTION_CLUT, VS_CLUT = 8 + 32, 6 + 16, 0 + 32
NAME_FONT, CAPTION_FONT, VS_FONT = 3, 1, 2
CAPTIONS = ('STAGE', 'FIGHT', 'PRACTICE MODE')
DIGITS = '0123456789'
FRAME_GREY = (232, 232, 232)
TINT_FLOOR = (192, 192, 192)
# modes.md: column edges of the tile grid, the first row, the caption bar (colour, width).
COLUMN_EDGES = (0, 69, 137, 206, 274, 343, 411)
GRID_TOP, ROW_H = 20, 64
BAR_AT, BAR_H, BAR_TAIL = (0, 36), 36, 110
BARS = {0: ((104, 0, 128), 216), 1: ((216, 0, 8), 216), 3: ((0, 192, 160), 216), 4: ((0, 192, 192), 248)}
# (background picture, caption bar): arcade and time attack, VS and demonstration, survival, practice.
SCREENS = ((0, 0), (1, 1), (0, 3), (0, 4))
VS_AT, CAPTION_AT = (162, 250), (28, 42)
# USA addresses of tables whose Japan Rev.1 addresses are in modes.md and draw_sim.py.
USA_RECORDS, USA_NAMES = 0x80097AB8, 0x80097D40
ARCADE_POSITIONS = struct.pack('<4h', 12, 100, -160, 0)   # the start of 0x800B4ED0 in Rev.1


def scale_x(x):
    return x * W // SOURCE_W


def scale_y(y):
    return y * H // SOURCE_H


def exe_word(exe, address):
    return struct.unpack_from('<I', exe, address - 0x80010000 + 0x800)[0]


def exe_string(exe, address):
    start = address - 0x80010000 + 0x800
    return exe[start:exe.index(b'\0', start)].decode('ascii')


def fighter_records(exe):
    """Per fighter id: face picture, facing bit, tint (RGB) and name, from costume 0."""
    records = []
    for ident in range(FIGHTERS):
        word = exe_word(exe, exe_word(exe, USA_RECORDS + 16 * ident))
        name = exe_string(exe, exe_word(exe, exe_word(exe, USA_NAMES + 16 * ident)))
        rgb = (word >> 8 & 255, word >> 16 & 255, word >> 24 & 255)
        records.append(dict(face=word & 31, facing=word >> 5 & 1, tint=rgb, name=name))
    return records


def positions(arcade):
    """Portrait and name positions per side (arcade.ovl, 0x800B4ED0 and 0x800B4EE0 in Rev.1)."""
    at = arcade.find(ARCADE_POSITIONS)
    if at < 0:
        raise ValueError('the VS position table is not in this arcade.ovl')
    values = struct.unpack_from('<16h', arcade, at)
    return dict(portraits=(values[0:2], values[4:6]), names=(values[8:10], values[12:14]))


def face_picture(tim):
    """FUN_800520A0 moves its CLUT on 64 entries per 64 texel rows; word 0 is transparent."""
    meta, indices, palette = parse_indexed_tim(tim)
    width, height = meta['width_pixels'], meta['height_pixels']
    pixels = np.frombuffer(indices, np.uint8).reshape(height, width).astype(np.uint32)
    bases = (np.arange(height) // 64 * 64)[:, None]
    words = np.array(palette, dtype=np.uint32)[(pixels + bases) & 255]
    expand = lambda v: (v << 3 | v >> 2).astype(np.uint8)        # noqa: E731
    return np.dstack((expand(words & 31), expand(words >> 5 & 31), expand(words >> 10 & 31),
                      (words & 0x7FFF != 0).astype(np.uint8) * 255))


def rgba_tim(tim):
    meta, indices, palette = parse_indexed_tim(tim)
    width, height = meta['width_pixels'], meta['height_pixels']
    words = np.array(palette, dtype=np.uint32)[np.frombuffer(indices, np.uint8).reshape(height, width)]
    expand = lambda v: (v << 3 | v >> 2).astype(np.uint8)        # noqa: E731
    return np.dstack((expand(words & 31), expand(words >> 5 & 31), expand(words >> 10 & 31),
                      (words != 0).astype(np.uint8) * 255))


def member_tim(member):
    return decompress_tim(member)[0] if struct.unpack_from('<I', member)[0] != 0x10 else member


def backdrop(tia):
    """FUN_80051C9C: 7 rows x 6 columns of 32-texel cells stretched onto 69 x 64 screen cells."""
    canvas = np.zeros((SOURCE_H, SOURCE_W, 3), np.uint8)
    members = tia_members(tia)
    for at, member in enumerate(members):
        row, column = at // 6, at % 6
        left, right = COLUMN_EDGES[column], COLUMN_EDGES[column + 1]
        cell = rgba_tim(member_tim(member))[:, :, :3]
        # The last column is 12 texels of its 32, and the screen ends inside it.
        shown = cell.shape[1] * (right - left) // 32 if cell.shape[1] < 32 else right - left
        image = Image.fromarray(cell).resize((max(shown, right - left) if cell.shape[1] == 32 else shown, ROW_H),
                                             Image.NEAREST)
        top = GRID_TOP + row * ROW_H
        pixels = np.asarray(image)[:, :max(0, SOURCE_W - left)]
        canvas[top:top + ROW_H, left:left + pixels.shape[1]] = pixels
    return canvas


def caption_bar(canvas, bar):
    """FUN_80051E74: opaque up to width - 110, then the colour fading out over the background."""
    colour, width = BARS[bar]
    x0, y0 = BAR_AT
    canvas[y0:y0 + BAR_H, x0:x0 + width - BAR_TAIL] = colour
    for x in range(width - BAR_TAIL, width):
        # Drawn subtractive then additive (modes 2 and 1, 0x800228F4): the fade replaces the picture's hue.
        fade = np.array(colour, float) * (width - x) / BAR_TAIL
        row = canvas[y0:y0 + BAR_H, x0 + x].astype(float)
        canvas[y0:y0 + BAR_H, x0 + x] = np.clip(np.clip(row - fade, 0, 255) + fade, 0, 255).astype(np.uint8)


def panel(canvas, face, record, at, mirrored):
    """FUN_800520A0: the additive glow drawn first, then the frame and the portrait over it."""
    x, y = at
    out = canvas[y:y + FRAME_H, x:x + FRAME_W].astype(float)
    out[:6], out[218:] = FRAME_GREY, FRAME_GREY
    out[:, :2], out[:, 128:] = FRAME_GREY, FRAME_GREY
    picture = face[PORTRAIT_TOP:PORTRAIT_TOP + PORTRAIT_H, :PORTRAIT_W]
    if mirrored:
        picture = picture[:, ::-1]
    inner = out[6:218, 2:128]
    # Black to the fighter's colour over the upper half, the colour to light grey over the lower.
    tint, floor = np.array(record['tint'], float), np.array(TINT_FLOOR, float)
    upper = np.linspace(0, 1, 106)[:, None, None] * tint
    lower = tint + np.linspace(0, 1, 106)[:, None, None] * (floor - tint)
    inner[:] = np.clip(inner + np.concatenate((upper, lower)), 0, 255)
    inner[:] = np.where(picture[:, :, 3:] > 0, picture[:, :, :3], inner)
    return out.astype(np.uint8)


def scaled(array, size):
    return Image.fromarray(array).resize(size, Image.LANCZOS)


def indexed(image, colours, first_code, dither):
    """Codes first_code.. and their RGB555 words, always exactly `colours` long."""
    codes, words = menu.quantise(image.convert('RGBA'), colours, dither)
    return codes.astype(np.int32) - 1 + first_code, list(words)


def text_palette(fonts, colour):
    return [menu.word(*menu.rgb555(v)) if v else 0x8000 for v in fonts.palette(colour)][1:]


def pack_text(fonts, string, font, first_code):
    glyphs = fonts.render(string, font)
    return np.where(glyphs, glyphs + first_code - 1, 0).astype(np.uint8)


def text_block(fonts, records):
    """Names first (colour 8, font 3), then captions and digits (colour 6, font 1)."""
    pictures = [pack_text(fonts, r['name'], NAME_FONT, NAME_CODE) for r in records]
    pictures += [pack_text(fonts, s, CAPTION_FONT, CAPTION_CODE) for s in (*CAPTIONS, *DIGITS)]
    width = max(p.shape[1] for p in pictures)
    height = max(p.shape[0] for p in pictures)
    if width > 255 or height > 255:
        raise ValueError('a VS text is too large for its record')
    body = bytearray()
    for picture in pictures:
        padded = np.zeros((height, width), np.uint8)
        padded[:picture.shape[0], :picture.shape[1]] = picture
        body += bytes((picture.shape[1], picture.shape[0])) + padded.tobytes()
    return bytes(body), width, height, len(pictures)


def paste_text(index, picture, x, y):
    area = index[y:y + picture.shape[0], x:x + picture.shape[1]]
    area[:] = np.where(picture, picture, area)


def name_position(at, width):
    """FUN_80052650: a negative x right-aligns; both ends are kept inside 12..356."""
    x = at[0]
    right, left = (-x, -x - width) if x < 0 else (x + width, x)
    left = max(left, 12)
    return left + 356 - right if right > 356 else left


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--records', type=Path, default=ROOT / 'assets/imported/vs-art-src/raw')
    p.add_argument('--arcade', type=Path, default=ROOT / 'assets/imported/menu-art-src/raw/005.bin')
    p.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--preview', default='', help='P1,P2 fighter ids: also write vs-preview.png')
    args = p.parse_args()
    out = args.out.resolve()
    if not any(root.resolve() in out.parents for root in (ROOT / 'assets/imported', ROOT / 'build')):
        p.error('output must be under assets/imported or build')
    out.mkdir(parents=True, exist_ok=True)
    from psx_disc_import import US_EXE_SHA
    exe = args.exe.read_bytes()
    if hashlib.sha256(exe).hexdigest() != US_EXE_SHA:
        p.error('needs the fingerprinted USA SLUS_004.02')
    records = fighter_records(exe)
    place = positions(args.arcade.read_bytes())
    fonts = menu.Fonts(exe)
    text_words = text_palette(fonts, NAME_CLUT) + text_palette(fonts, CAPTION_CLUT) + text_palette(fonts, VS_CLUT)

    pictures = [backdrop((args.records / f'{12 + b:03}.bin').read_bytes()) for b in (0, 1)]
    faces = [face_picture(decompress_tim((args.records / f'{14 + r["face"]:03}.bin').read_bytes())[0]) for r in records]

    backgrounds = []
    for number, (picture, bar) in enumerate(SCREENS):
        canvas = pictures[picture].copy()
        caption_bar(canvas, bar)
        index, words = indexed(scaled(canvas, (W, H)), BACKGROUND_COLOURS, 0, True)
        index = index.astype(np.uint8)
        vs = pack_text(fonts, 'VS', VS_FONT, VS_CODE)
        paste_text(index, vs, scale_x(VS_AT[0]), scale_y(VS_AT[1]))
        palette = (words + text_words + [CREDIT_WORD] + [0x8000] * 256)[:256]
        (out / f'VSBG{number}.BIN').write_bytes(b'T3BG0001' + struct.pack('>HH', W, H) +
                                                struct.pack('>256H', *palette) + index.tobytes())
        backgrounds.append((index, palette))

    panels = bytearray()
    for picture in range(2):
        for side in range(2):
            for ident, record in enumerate(records):
                mirrored = bool(record['facing'] ^ side)
                baked = panel(pictures[picture], faces[ident], record, place['portraits'][side], mirrored)
                codes, words = indexed(scaled(baked, (PANEL_W, PANEL_H)), PANEL_COLOURS, 0, True)
                panels += struct.pack(f'>{PANEL_COLOURS}H', *words) + codes.astype(np.uint8).tobytes()
    (out / 'VSPANEL.BIN').write_bytes(bytes(panels))

    text, text_w, text_h, text_count = text_block(fonts, records)
    (out / 'VSTEXT.BIN').write_bytes(text)
    panel_bytes = 2 * PANEL_COLOURS + PANEL_W * PANEL_H
    layout = dict(VS_W=W, VS_H=H, VS_PANEL_W=PANEL_W, VS_PANEL_H=PANEL_H, VS_PANEL_COLOURS=PANEL_COLOURS,
                  VS_PANEL_BYTES=panel_bytes, VS_SIDE_CODE_0=SIDE_CODES[0], VS_SIDE_CODE_1=SIDE_CODES[1],
                  VS_CREDIT_CODE=CREDIT_CODE, VS_TEXTS=text_count, VS_TEXT_W=text_w, VS_TEXT_H=text_h, VS_FIGHTERS=FIGHTERS,
                  VS_CAPTION_X=scale_x(CAPTION_AT[0]), VS_CAPTION_Y=scale_y(CAPTION_AT[1]),
                  VS_PORTRAIT_X0=scale_x(place['portraits'][0][0]), VS_PORTRAIT_Y0=scale_y(place['portraits'][0][1]),
                  VS_PORTRAIT_X1=scale_x(place['portraits'][1][0]), VS_PORTRAIT_Y1=scale_y(place['portraits'][1][1]),
                  VS_NAME_Y0=scale_y(place['names'][0][1]), VS_NAME_Y1=scale_y(place['names'][1][1]))
    names_x = [[scale_x(name_position(place['names'][side], 21 * len(r['name']))) for r in records] for side in range(2)]
    lines = ['/* Generated by scripts/pack_vs_art.py: where the VS screen puts things. */',
             'enum { ' + ', '.join(f'{k}={v}' for k, v in layout.items()) + ' };',
             'static const uint16_t g_vs_name_x[2][VS_FIGHTERS]={']
    lines += ['    {' + ','.join(map(str, row)) + '},' for row in names_x] + ['};']
    (out / 'vs_layout.h').write_text('\n'.join(lines) + '\n')

    if args.preview:
        ids = [int(v) for v in args.preview.split(',')]
        index, palette = (a.copy() for a in (backgrounds[0][0], np.array(backgrounds[0][1])))
        table = np.array(palette, dtype=np.uint32)
        for side, ident in enumerate(ids):
            at = (2 * side * FIGHTERS + ident) * panel_bytes
            record = panels[at:at + panel_bytes]
            words = struct.unpack(f'>{PANEL_COLOURS}H', record[:2 * PANEL_COLOURS])
            table[SIDE_CODES[side]:SIDE_CODES[side] + PANEL_COLOURS] = words
            codes = np.frombuffer(record, np.uint8, offset=2 * PANEL_COLOURS).reshape(PANEL_H, PANEL_W) + SIDE_CODES[side]
            x, y = scale_x(place['portraits'][side][0]), scale_y(place['portraits'][side][1])
            index[y:y + PANEL_H, x:x + PANEL_W] = codes
            name = pack_text(fonts, records[ident]['name'], NAME_FONT, NAME_CODE)
            paste_text(index, name, names_x[side][ident], scale_y(place['names'][side][1]))
        caption = pack_text(fonts, 'STAGE', CAPTION_FONT, CAPTION_CODE)
        paste_text(index, caption, scale_x(CAPTION_AT[0]), scale_y(CAPTION_AT[1]))
        words = table[index]
        rgb = np.dstack(((words & 31) << 3, (words >> 5 & 31) << 3, (words >> 10 & 31) << 3)).astype(np.uint8)
        Image.fromarray(rgb).resize((W * 2, H)).save(out / 'vs-preview.png')
    print(json.dumps(layout))


if __name__ == '__main__':
    main()
