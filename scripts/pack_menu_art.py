#!/usr/bin/env python3
"""Pack the 704x448 title and character select art from the local USA disc.

Inputs (all private, from scripts/psx_disc_import.py --extract-ids 9,10 and
--dump-exe): select.ovl (record 9), title.ovl (record 10), SLUS_004.02.
Layout rules and picture archives follow Tekken3Remake (n0madic, PolyForm
Noncommercial 1.0.0; see NOTICE.md), docs/research/code/modes.md and
tools/research/select_sim.py. Output is private and never enters git.

Everything big is baked into two 352x448 VDP2 bitmaps (shown at 2x horizontal
zoom on a 704x448 screen); only the parts that move are VDP1 sprites:

  TITLEBG.BIN / SELBG.BIN   'T3BG0001', u16 width, u16 height (big endian),
                            256 RGB555 words (bit 15 set), width*height bytes.
  PORT00..20.BIN            116 RGB555 words, 128x208 bytes (126 used), codes 1..116,
                            0 transparent: one big portrait per fighter id.
  menu_layout.h             the coordinates the art was composed with.
"""
import argparse
import hashlib
import struct
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
sys.path.insert(0, str(ROOT / 'scripts'))
import overlay_images  # noqa: E402
import system_textures  # noqa: E402

W, H = 352, 448
PORT_W, PORT_H, PORT_COLOURS = 126, 208, 116
PORT_STRIDE = 128                       # VDP1 textures are a multiple of 8 wide
FACES, FACE_W, FACE_H = 20, 32, 58
CELL_W, CELL_H, PITCH = 28, 50, 34


def rgb555(value):
    return ((value & 31) * 255 // 31, (value >> 5 & 31) * 255 // 31, (value >> 10 & 31) * 255 // 31)


def word(r, g, b):
    return 0x8000 | (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10


def archives(record, minimum):
    data = Path(record).read_bytes()
    return [f.tims for f in overlay_images.scan(data, 0x80100000) if f.archive and len(f.tims) >= minimum]


def tim_image(tim, tmp):
    """RGBA of a TIM member (RGB 0 is transparent, as on the PS1)."""
    path = Path(tmp) / 'x.png'
    path.unlink(missing_ok=True)
    overlay_images.to_png(tim, path)
    rgb = Image.open(path).convert('RGB')
    arr = np.asarray(rgb)
    alpha = ((arr.sum(axis=2) > 0) * 255).astype(np.uint8)
    return Image.fromarray(np.dstack([arr, alpha]), 'RGBA')


def portrait(tim):
    """126x208 RGBA: 8-bit pixels use a 64-entry palette per 64 lines (CLUT field + 4)."""
    blocks = {b[0]: b for b in system_textures.tim_blocks(tim)}
    palette = struct.unpack_from(f'<{blocks["clut"][3]}H', blocks['clut'][5])
    _, _, _, words, rows, data = blocks['image']
    pix = np.frombuffer(data, np.uint8)[:words * 2 * rows].reshape(rows, words * 2)
    out = np.zeros((PORT_H, PORT_W, 4), np.uint8)
    for y in range(PORT_H):
        base = 64 * (y // 64)
        for x in range(PORT_W):
            value = palette[(base + int(pix[y, x])) & 255]
            if value & 0x7FFF:
                out[y, x] = (*rgb555(value), 255)
    return Image.fromarray(out, 'RGBA')


def quantise(image, colours, dither):
    """(indices 1..colours with 0 transparent, palette words)."""
    rgb = image.convert('RGB')
    q = rgb.quantize(colors=colours, method=Image.Quantize.MEDIANCUT,
                     dither=Image.Dither.FLOYDSTEINBERG if dither else Image.Dither.NONE)
    pal = (q.getpalette() + [0] * 768)[:colours * 3]
    idx = np.asarray(q, np.uint8) + 1
    idx[np.asarray(image.getchannel('A')) == 0] = 0
    return idx, [word(*pal[3 * i:3 * i + 3]) for i in range(colours)]


def background(canvas, name, out):
    """Entry 0 is black, so padding and fades never show a stray colour."""
    q = canvas.convert('RGB').quantize(colors=255, method=Image.Quantize.MEDIANCUT,
                                       dither=Image.Dither.NONE)
    pal = [0, 0, 0] + (q.getpalette() + [0] * 768)[:765]
    words = [word(*pal[3 * i:3 * i + 3]) for i in range(256)]
    indices = (np.asarray(q, np.uint8) + 1).astype(np.uint8)
    (out / name).write_bytes(b'T3BG0001' + struct.pack('>HH', canvas.width, canvas.height) +
                             struct.pack('>256H', *words) + indices.tobytes())


def trim(image):
    box = image.getchannel('A').getbbox()
    return image.crop(box), box


def title(record, out, tmp):
    tims = next(t for t in archives(record, 15) if len(t) == 15)
    picture = Image.new('RGB', (368, 480))
    for i in range(6):
        piece = tim_image(tims[i], tmp).convert('RGB')
        picture.paste(piece, ((i % 3) * 128, 0 if i < 3 else 256))
    canvas = picture.resize((W, H), Image.Resampling.LANCZOS)
    namco = tim_image(tims[6], tmp)                      # 72 x 16 at (151, 388) of 368 x 480
    canvas.paste(namco.convert('RGB'), (151 * W // 368, 388 * H // 480))
    background(canvas, 'TITLEBG.BIN', out)
    canvas.save(out / 'title-bg.png')
    return {'TITLE_NAMCO_X': 151 * W // 368, 'TITLE_NAMCO_Y': 388 * H // 480,
            'TITLE_PROMPT_Y': 364 * H // 480, 'TITLE_COPY_Y0': 412 * H // 480,
            'TITLE_COPY_Y1': 432 * H // 480}


def select(record, exe, out, tmp):
    frame = next(t for t in archives(record, 40) if len(t) == 48)
    pic = lambda i: tim_image(frame[i], tmp)                        # noqa: E731
    tile = pic(20).convert('RGB')
    canvas = Image.new('RGB', (W, H))
    for y in range(0, H, tile.height):
        for x in range(0, W, tile.width):
            canvas.paste(tile, (x, y))
    logo, box = trim(pic(14))
    lx = (W - logo.width) // 2
    canvas.paste(logo.convert('RGB'), (lx, 2), logo.getchannel('A'))
    py0 = 2 + logo.height + 2
    port_y = py0 + 14
    plate_y = port_y + PORT_H
    left_x, right_x = 10, W - PORT_W - 10
    for tim, x0, x1, dest in ((frame[10], 0, 146, 0), (frame[11], 38, 184, W - 146)):
        strip = tim_image(tim, tmp).crop((x0, 0, x1, 14))
        canvas.paste(strip.convert('RGB'), (dest, py0))
    for tim, x0, x1, dest in ((frame[16], 0, 146, 0), (frame[19], 38, 184, W - 146)):
        strip = tim_image(tim, tmp).crop((x0, 6, x1, 24))
        canvas.paste(strip.convert('RGB'), (dest, plate_y))
    draw = ImageDraw.Draw(canvas)
    for x in (left_x, right_x):
        draw.rectangle((x - 2, port_y, x + PORT_W + 1, plate_y - 1), fill=(0, 0, 0))
    grid_y = plate_y + 18 + 2
    grid_x = (W - 10 * PITCH + (PITCH - CELL_W)) // 2
    tims = system_textures.members(exe.read_bytes(), 'usa')
    # the two rows follow t3_select_grid's cell table, written by the C side;
    # here every fighter id with a HUD face is placed by the same table.
    grid = [[7, 4, 5, 1, 6, 8, 0, 3, 2, 9], [19, 12, 11, 13, 14, 20, 10, 16, 15, 18]]  # select_grid.c
    faces = {}
    for ident in range(FACES):
        blocks = {b[0]: b for b in system_textures.tim_blocks(tims[9 + ident][1])}
        palette = struct.unpack_from('<256H', blocks['clut'][5])
        _, _, _, words, rows, data = blocks['image']
        pix = np.frombuffer(data, np.uint8)[:words * 2 * rows].reshape(rows, words * 2)
        face = Image.new('RGB', (FACE_W, FACE_H))
        face.putdata([rgb555(palette[int(v)]) for v in pix.flatten()])
        faces[ident] = face.resize((CELL_W, CELL_H), Image.Resampling.LANCZOS)
    for row, ids in enumerate(grid):
        y = grid_y + row * (CELL_H + 2)
        for column, ident in enumerate(ids):
            x = grid_x + column * PITCH
            draw.rectangle((x - 1, y - 1, x + CELL_W, y + CELL_H), fill=(10, 10, 14))
            canvas.paste(faces[14 if ident == 20 else ident], (x, y))
    background(canvas, 'SELBG.BIN', out)
    canvas.save(out / 'select-bg.png')
    return {'SEL_PORT_Y': port_y, 'SEL_PORT_LEFT_X': left_x, 'SEL_PORT_RIGHT_X': right_x,
            'SEL_PLATE_Y': plate_y, 'SEL_GRID_X': grid_x, 'SEL_GRID_Y': grid_y,
            'SEL_CELL_W': CELL_W, 'SEL_CELL_H': CELL_H, 'SEL_PITCH': PITCH,
            'SEL_ROW_PITCH': CELL_H + 2, 'SEL_LOGO_H': logo.height}


def portraits(record, out):
    tims = next(t for t in archives(record, 22) if len(t) == 22)
    sheet = Image.new('RGB', (7 * PORT_W, 3 * PORT_H))
    for ident in range(21):
        image = portrait(tims[ident])
        idx, words = quantise(image, PORT_COLOURS, True)
        (out / f'PORT{ident:02}.BIN').write_bytes(struct.pack(f'>{PORT_COLOURS}H', *words) +
                                                  np.pad(idx, ((0, 0), (0, PORT_STRIDE - PORT_W))).tobytes())
        flat = Image.new('RGB', image.size, (30, 0, 30))
        flat.paste(image.convert('RGB'), (0, 0), image.getchannel('A'))
        sheet.paste(flat, ((ident % 7) * PORT_W, (ident // 7) * PORT_H))
    sheet.save(out / 'portraits.png')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--select', type=Path, default=ROOT / 'assets/imported/menu-art-src/raw/009.bin')
    parser.add_argument('--title', type=Path, default=ROOT / 'assets/imported/allchars-demo/raw/010.bin')
    parser.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if not any(root.resolve() in out.parents for root in (ROOT / 'assets/imported', ROOT / 'build')):
        parser.error('output must be under assets/imported or build')
    out.mkdir(parents=True, exist_ok=True)
    from psx_disc_import import US_EXE_SHA
    if hashlib.sha256(args.exe.read_bytes()).hexdigest() != US_EXE_SHA:
        parser.error('needs the fingerprinted USA SLUS_004.02')
    with tempfile.TemporaryDirectory() as tmp:
        layout = title(args.title, out, tmp)
        layout.update(select(args.select, args.exe, out, tmp))
    portraits(args.select, out)
    lines = ['/* Generated by scripts/pack_menu_art.py: where the baked art puts things. */',
             f'enum {{ MENU_BG_W={W}, MENU_BG_H={H}, MENU_PORT_W={PORT_W}, MENU_PORT_H={PORT_H},']
    lines += [f'       {k}={v},' for k, v in layout.items()]
    lines.append('       MENU_LAYOUT_END };')
    (out / 'menu_layout.h').write_text('\n'.join(lines) + '\n')
    print(layout)


if __name__ == '__main__':
    main()
