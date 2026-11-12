#!/usr/bin/env python3
"""Pack the 704x448 title and character select art from the local USA disc.

Inputs (all private, from scripts/psx_disc_import.py --extract-ids 9,10 and
--dump-exe): select.ovl (record 9), title.ovl (record 10), SLUS_004.02.
Layout rules and picture archives follow Tekken3Remake (n0madic, PolyForm
Noncommercial 1.0.0; see NOTICE.md), docs/research/code/modes.md and
tools/research/select_sim.py. Output is private and never enters git.

Everything big is baked into two 352x448 VDP2 bitmaps (shown at 2x horizontal
zoom on a 704x448 screen); only the parts that move are VDP1 sprites:

  TITLEBG.BIN / SELBG.BIN / TILEBG.BIN   'T3BG0001', u16 width, u16 height (big endian),
                            256 RGB555 words (bit 15 set), width*height bytes.
  PORT00..21.BIN            glow word + pad word, 116 RGB555 words, 128x208 bytes (126 used), codes 1..116,
                            0 transparent: one big portrait per fighter id.
  LOAD.BIN                  original NOW LOADING glyphs, LUT4 with their own palette.
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
PORT_W, PORT_H, PORT_COLOURS = 126, 208, 92
PORT_STRIDE = 128                       # VDP1 textures are a multiple of 8 wide
FACES, FACE_W, FACE_H = 20, 32, 58
# The Force result's boss faces on the 352 x 448 backdrop: 32 x 58 PS1 pixels mapped as the menu maps
# the 368 x 480 screen, in 63 colours each (codes 1..63; the menu adds 64 per boss).
FACE_BG_W, FACE_BG_H, FACE_COLOURS = 31, 54, 63
CELL_W, CELL_H, PITCH = 28, 50, 34
TILE_MAP = 0x5F864                       # select.ovl 0x80118BDC - 0x800B9378, 8 x 8 tile indices
NAME_W, NAME_H = 96, 16


def rgb555(value):
    return ((value & 31) * 255 // 31, (value >> 5 & 31) * 255 // 31, (value >> 10 & 31) * 255 // 31)


def word(r, g, b):
    return 0x8000 | (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10


def archives(record, minimum):
    data = Path(record).read_bytes()
    return [f.tims for f in overlay_images.scan(data, 0x80100000) if f.archive and len(f.tims) >= minimum]


def hud_face(tims, ident):
    """HUD face `ident` (system texture 9 + ident, as FUN_8004BBC0 draws it) as RGB."""
    blocks = {b[0]: b for b in system_textures.tim_blocks(tims[9 + ident][1])}
    palette = struct.unpack_from('<256H', blocks['clut'][5])
    _, _, _, words, rows, data = blocks['image']
    pix = np.frombuffer(data, np.uint8)[:words * 2 * rows].reshape(rows, words * 2)
    face = Image.new('RGB', (FACE_W, FACE_H))
    face.putdata([rgb555(palette[int(v)]) for v in pix.flatten()])
    return face


def faces(exe, out):
    """FACES.BIN: every HUD face for the Force result's boss list (draw_sim.force_screen_draw).

    'T3FC0001', u16 faces, u16 w, u16 h; per face FACE_COLOURS palette words, then w*h codes 1..63."""
    tims = system_textures.members(exe, 'usa')
    blob = bytearray(b'T3FC0001' + struct.pack('>HHH', FACES, FACE_BG_W, FACE_BG_H))
    for ident in range(FACES):
        small = hud_face(tims, ident).resize((FACE_BG_W, FACE_BG_H), Image.Resampling.LANCZOS)
        quantised = small.quantize(colors=FACE_COLOURS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        rgb = quantised.getpalette()[:3 * FACE_COLOURS]
        rgb += [0] * (3 * FACE_COLOURS - len(rgb))
        blob += struct.pack(f'>{FACE_COLOURS}H', *(word(*rgb[3 * i:3 * i + 3]) for i in range(FACE_COLOURS)))
        blob += (np.asarray(quantised, np.uint8) + 1).tobytes()
    (out / 'FACES.BIN').write_bytes(bytes(blob))
    return {'MENU_FACES': FACES, 'MENU_FACE_W': FACE_BG_W, 'MENU_FACE_H': FACE_BG_H, 'MENU_FACE_COLOURS': FACE_COLOURS}


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


def background(canvas, name, out, flat=False):
    """Entry 0 is transparent (and black); fully transparent pixels use it."""
    rgba = canvas.convert('RGBA')
    q = rgba.convert('RGB').quantize(colors=255, method=Image.Quantize.MEDIANCUT,
                                     dither=Image.Dither.NONE)
    pal = [0, 0, 0] + (q.getpalette() + [0] * 768)[:765]
    words = [word(*pal[3 * i:3 * i + 3]) for i in range(256)]
    indices = (np.asarray(q, np.uint8) + 1).astype(np.uint8)
    indices[np.asarray(rgba.getchannel('A')) == 0] = 0
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
    # The steel tiles scroll on their own layer (TILEBG.BIN): the original's 8 x 8 map of 48 x 64
    # tiles (select.ovl 0x80118BDC, ten tile pictures = frame members 20..29), the first column
    # starting 8 dots off the left edge. 8 rows of 64 make the 512-high bitmap that wraps.
    rows = Path(record).read_bytes()[TILE_MAP:TILE_MAP + 64]
    assert max(rows) < 10, 'tile map not where select_sim.py puts it'
    tile_pics = [pic(20 + i).convert('RGB') for i in range(10)]
    tiles = Image.new('RGB', (W, 512))
    for row in range(8):
        for column in range(8):
            tiles.paste(tile_pics[rows[row * 8 + column]], (column * 48 - 8, row * 64))
    background(tiles, 'TILEBG.BIN', out)
    canvas = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    # Layout from the PS1 capture (368 x 480 scaled to 352 x 448): the glass panels start near y 56,
    # the plates sit at 300 with the portraits' shoulders reaching them, the grid below. The
    # PLAYER SELECT picture is ghosted behind the panels, its black plate see-through.
    logo, box = trim(pic(14))
    lx = (W - logo.width) // 2
    mask = logo.getchannel('A').point(lambda v: v)
    luma = logo.convert('L').point(lambda v: min(255, v * 3))
    canvas.paste(logo.convert('RGB'), (lx, 24), luma)
    port_y = 56
    py0 = port_y - 14
    plate_y = 300
    left_x, right_x = 10, W - PORT_W - 10
    for tim, x0, x1, dest in ((frame[10], 0, 146, 0), (frame[11], 38, 184, W - 146)):
        strip = tim_image(tim, tmp).crop((x0, 0, x1, 14))
        canvas.paste(strip.convert('RGB'), (dest, py0))
    draw = ImageDraw.Draw(canvas)
    for x in (left_x, right_x):                   # name plates: white fading to grey, black text
        for row in range(16):
            level = 250 - row * 6
            draw.line((x, plate_y + row, x + PORT_W - 1, plate_y + row), fill=(level, level, level, 255))
    for tim, x0, x1, dest in ((frame[16], 0, 146, 0), (frame[19], 38, 184, W - 146)):
        strip = tim_image(tim, tmp).crop((x0, 8, x1, 22))
        canvas.paste(strip.convert('RGB'), (dest, plate_y + 16))
    grid_y = plate_y + 16 + 14 + 4
    grid_x = (W - 10 * PITCH + (PITCH - CELL_W)) // 2
    tims = system_textures.members(exe.read_bytes(), 'usa')
    # the two rows follow t3_select_grid's cell table, written by the C side;
    # here every fighter id with a HUD face is placed by the same table.
    grid = [[7, 4, 5, 1, 6, 8, 0, 3, 2, 9], [19, 12, 11, 13, 14, 20, 10, 16, 15, 18]]  # select_grid.c
    faces = {}
    for ident in range(FACES):
        faces[ident] = hud_face(tims, ident).resize((CELL_W, CELL_H), Image.Resampling.LANCZOS)
    for row, ids in enumerate(grid):
        y = grid_y + row * (CELL_H + 2)
        for column, ident in enumerate(ids):
            x = grid_x + column * PITCH
            draw.rectangle((x - 1, y - 1, x + CELL_W, y + CELL_H), fill=(10, 10, 14, 255))
            canvas.paste(faces[14 if ident == 20 else ident], (x, y))
    background(canvas, 'SELBG.BIN', out)
    canvas.save(out / 'select-bg.png')
    # the logo's black plate stays opaque: it is pasted whole above
    return {'SEL_PORT_Y': port_y, 'SEL_PORT_LEFT_X': left_x, 'SEL_PORT_RIGHT_X': right_x,
            'SEL_PLATE_Y': plate_y, 'SEL_GRID_X': grid_x, 'SEL_GRID_Y': grid_y,
            'SEL_CELL_W': CELL_W, 'SEL_CELL_H': CELL_H, 'SEL_PITCH': PITCH,
            'SEL_ROW_PITCH': CELL_H + 2, 'SEL_LOGO_H': logo.height,
            'SEL_SPRITE_Y': plate_y - PORT_H, 'SEL_GLOW_H': plate_y - port_y}


def glow_colour(image):
    """The panel colour behind a portrait: its dominant hue, made vivid."""
    import colorsys
    arr = np.asarray(image.convert('RGBA'))[60:170]
    opaque = arr[arr[..., 3] > 0][:, :3].astype(float) / 255
    r, g, b = opaque.mean(axis=0) if len(opaque) else (0.5, 0.5, 0.5)
    h, sat, v = colorsys.rgb_to_hsv(r, g, b)
    r, g, b = colorsys.hsv_to_rgb(h, min(1.0, sat * 1.5 + 0.25), 0.95)
    return word(int(r * 255), int(g * 255), int(b * 255))


def silhouette(tim):
    """The 'nobody yet' picture: black where the mask is bright, dark grey on its soft edge."""
    blocks = {b[0]: b for b in system_textures.tim_blocks(tim)}
    _, _, _, words, rows, data = blocks['image']
    pix = np.frombuffer(data, np.uint8)[:words * 2 * rows].reshape(rows, words * 2)[:PORT_H, :PORT_W]
    palette = struct.unpack_from(f'<{blocks["clut"][3]}H', blocks['clut'][5])
    level = np.array([(palette[int(v) & 255] & 31) for v in pix.flatten()]).reshape(pix.shape)
    out = np.zeros(pix.shape, np.uint8)
    out[level >= 6] = 2
    out[level >= 20] = 1
    return out


def names(record, out, tmp):
    """NAMES.BIN: per fighter the original name picture (black glyph mask, 96 x 16) and its extent.

    The sheet is frame member 44: 16-high rows, fighters 0-15 down the left, 16-19 on the right
    (True Ogre shares Ogre's name)."""
    frame = next(t for t in archives(record, 40) if len(t) == 48)
    sheet = np.asarray(tim_image(frame[44], tmp).convert('RGB')).sum(axis=2) > 60
    head, body = bytearray(), bytearray()
    for ident in range(21):
        name = 14 if ident == 20 else ident
        x0, row = (0, name) if name < 16 else (128, name - 16)
        cell = sheet[row * NAME_H:(row + 1) * NAME_H, x0:x0 + NAME_W]
        cols = np.where(cell.any(axis=0))[0]
        head += bytes((int(cols.min()), int(cols.max()) + 1))
        body += cell.astype(np.uint8).tobytes()
    (out / 'NAMES.BIN').write_bytes(bytes(head) + bytes(6) + bytes(body))


# ---- text in the original fonts ----------------------------------------------------------------
FONT_TABLE = 0x80021D64                  # USA: 4 x 16 bytes, glyph UV table first (extract_psx_hud.py)
MODES = ('ARCADE', 'VS', 'TEAM BATTLE', 'TIME ATTACK', 'SURVIVAL', 'TEKKEN FORCE', 'PRACTICE', 'OPTION')
# CLUT rows of the system fonts (rows 480+, x 256..): picked against the PS1 capture.
GOLD, RED, CYAN, WHITE, YELLOW = 5, 2, 3, 6, 39
DIM = 0.6


class Fonts:
    """Strings drawn with the original fonts: glyphs from the system texture VRAM, indices 0-15."""

    def __init__(self, exe):
        self.exe = exe
        self.vram = system_textures.build_vram(system_textures.members(exe, 'usa'))

    def word(self, x, y):
        return struct.unpack_from('<H', self.vram, 2 * (1024 * y + x))[0]

    def palette(self, k):
        return [self.word(256 + 16 * (k & 15) + i, ((k >> 4) + 24) | 480) for i in range(16)]

    def advance(self, font):
        return struct.unpack_from('<I5H2B', self.exe, FONT_TABLE - 0x80010000 + 0x800 + 16 * font)[1]

    def render(self, text, font):
        table, advance, _, width, height, page, _, _ = struct.unpack_from(
            '<I5H2B', self.exe, FONT_TABLE - 0x80010000 + 0x800 + 16 * font)
        px, py = (page & 15) * 64, ((page >> 4) & 1) * 256
        out = np.zeros((height, advance * len(text) + width), np.uint8)
        for n, char in enumerate(text):
            if char == ' ':
                continue
            uv, = struct.unpack_from('<H', self.exe, table - 0x80010000 + 0x800 + 2 * ord(char))
            u0, v0 = uv & 255, uv >> 8
            for y in range(height):
                for x in range(width):
                    packed = self.word(px + (u0 + x) // 4, py + v0 + y)
                    index = (packed >> (4 * ((u0 + x) & 3))) & 15
                    if index:       # glyphs are wider than their advance: never erase the last one's edge
                        out[y, n * advance + x] = index
        cols = np.where(out.any(axis=0))[0]
        return out[:, cols.min():cols.max() + 1]


def dimmed(words):
    return [word(*(int(c * DIM) for c in rgb555(w))) if w else 0 for w in words]


def texts(exe, out):
    """TEXT.BIN: every menu string as 4-bit glyph indices turned into VDP1 codes.

    'T3TX0001', u16 palettes, u16 textures; palettes (group, base code, 16 words); textures
    (group, palette, w, h, w*h codes). Group 0 is the title and mode list (palettes at codes
    16, 32 ...), group 1 the select (yellow, cyan, white at 208, 224, 240)."""
    fonts = Fonts(exe)
    base = lambda w: [word(*rgb555(v)) if v else 0 for v in w]            # noqa: E731
    colours = {'gold': fonts.palette(GOLD), 'red': fonts.palette(RED), 'cyan': fonts.palette(CYAN),
               'white': fonts.palette(WHITE), 'yellow': fonts.palette(YELLOW)}
    colours['dimred'], colours['dimcyan'] = dimmed(colours['red']), dimmed(colours['cyan'])
    colours = {k: [(v & 0x7FFF) | 0x8000 if v else 0 for v in w] for k, w in colours.items()}
    title_blocks = ['gold', 'red', 'cyan', 'white', 'dimred', 'dimcyan']
    select_blocks = {'yellow': 208, 'cyan': 224, 'white': 240}
    palettes = [(0, 16 * (i + 1), colours[name]) for i, name in enumerate(title_blocks)]
    palettes += [(1, code, colours[name]) for name, code in select_blocks.items()]
    code_of = {(0, name): 16 * (i + 1) for i, name in enumerate(title_blocks)}
    code_of.update({(1, name): code for name, code in select_blocks.items()})
    pictures, names = [], []

    def add(name, group, block, font, string):
        glyphs = fonts.render(string, font)
        height, width = glyphs.shape
        width8 = (width + 7) & ~7
        codes = np.zeros((height, width8), np.uint8)
        # glyph index i uses palette entry i of its block (entry 0 is transparent): code = base + i
        codes[:, :width] = np.where(glyphs > 0, glyphs + code_of[(group, block)], 0)
        pictures.append(struct.pack('>BBHH', group, 0, width8, height) + codes.tobytes())
        names.append((name, group, width, height))

    add('NAMCO', 0, 'red', 0, 'NAMCO')
    add('PRESENTS', 0, 'white', 0, 'PRESENTS.')
    add('PROMPT_ONE', 0, 'gold', 1, 'PUSH P1 START BUTTON')
    add('PROMPT_BOTH', 0, 'gold', 1, 'PUSH P1 AND/OR P2 BUTTON')
    add('COPYRIGHT', 0, 'white', 0, '@1994 1995 1996 1998 NAMCO LTD.')
    add('RESERVED', 0, 'white', 0, 'ALL RIGHTS RESERVED')
    add('NOW_LOADING', 0, 'white', 0, 'NOW LOADING...')
    # Standalone loading graphic for FORCE.BIN: the disc's original glyphs
    # and palette, LUT4 so it survives changes to the stage/HUD CRAM banks.
    glyphs = fonts.render('NOW LOADING...', 0)
    height, width = glyphs.shape
    padded = np.zeros((height, (width + 7) & ~7), np.uint8)
    padded[:, :width] = glyphs
    packed = (padded[:, 0::2] << 4) | padded[:, 1::2]
    (out / 'LOAD.BIN').write_bytes(b'T3LD0001' +
        struct.pack('>HH16H', padded.shape[1], height, *colours['white']) + packed.tobytes())
    for number, mode in enumerate(MODES):
        label = mode + ' MODE'
        normal = 'cyan' if mode == 'OPTION' else 'red'
        add(f'MODE{number}_SELECTED', 0, 'gold', 1, label)
        add(f'MODE{number}_NORMAL', 0, normal, 1, label)
        add(f'MODE{number}_DIM', 0, 'dim' + normal, 1, label)
    add('INSERT_COIN', 1, 'cyan', 0, 'INSERT COIN')
    for tag in ('1P', '2P', 'CPU'):
        add(f'TAG_{tag}', 1, 'white', 0, tag)
    for digit in range(10):
        add(f'DIGIT{digit}', 1, 'yellow', 2, str(digit))
    # Group 2: the Tekken Force result (result.ovl FUN_800F1F08). Its format strings name the font and
    # the colour; the CLUT is the colour plus the font's CLUT offset (hud.md: 16 for font 1, 32 for
    # font 2): the title 32, font 1's colours 2, 5, 6, 8 at 18, 21, 22, 24. Codes 112..191: the second
    # portrait slot, unused on that screen.
    for i, k in enumerate((32, 18, 21, 22, 24)):
        palettes.append((2, 112 + 16 * i, [(v & 0x7FFF) | 0x8000 if v else 0 for v in fonts.palette(k)]))
        code_of[(2, f'c{k}')] = 112 + 16 * i
    for name, colour, font, string in (
            ('FR_TITLE', 0, 2, 'TEKKEN FORCE'), ('FR_SCORE', 5, 1, 'YOUR SCORE:'),
            ('FR_HIGH', 5, 1, 'HIGH SCORE:'), ('FR_RECORD6', 6, 1, 'NEW RECORD!!'),
            ('FR_RECORD2', 2, 1, 'NEW RECORD!!'), ('FR_SAVED', 8, 1, 'YOU SAVED'),
            ('FR_DOCTOR6', 6, 1, 'DR.BOSKONOVITCH!'), ('FR_DOCTOR2', 2, 1, 'DR.BOSKONOVITCH!'),
            ('FR_KEYS', 5, 1, 'KEYS:'), ('FR_COPPER', 2, 1, 'COPPER'), ('FR_SILVER', 6, 1, 'SILVER'),
            ('FR_GOLD', 5, 1, 'GOLD'), ('FR_CONTINUED', 8, 1, 'TO BE CONTINUED...'),
            ('FR_BOSS', 5, 1, 'BOSS:'), ('FR_FORCE', 5, 1, 'FORCE:')):
        add(name, 2, f'c{colour + 16 * font}', font, string)
    for digit in range(10):
        add(f'FR_DIGIT{digit}', 2, 'c22', 1, str(digit))
    blob = bytearray(b'T3TX0001' + struct.pack('>HH', len(palettes), len(pictures)))
    for group, code, words in palettes:
        blob += struct.pack('>BB16H', group, code, *words)
    for picture in pictures:
        blob += picture
    (out / 'TEXT.BIN').write_bytes(bytes(blob))
    lines = ['/* Generated by scripts/pack_menu_art.py: ids of the pictures in TEXT.BIN. */', 'enum {']
    lines += [f'    TX_{name}={i},' for i, (name, *_rest) in enumerate(names)]
    lines += [f'    TX_COUNT={len(names)},']
    lines += ['    TX_GROUP0=%d,' % sum(1 for n in names if n[1] == 0)]
    # font 1's advance, for the result's right-aligned %8D / %4D numbers and the second part of YOU SAVED
    lines += ['    TX_FONT1_ADVANCE=%d,' % fonts.advance(1), '};']
    lines += ['/* width, height of the original glyph rows (before padding to 8) */',
              'static const unsigned short g_text_size[TX_COUNT][2]={'] + \
        [f'    {{{w},{h}}},' for _, _, w, h in names] + ['};']
    (out / 'menu_text.h').write_text('\n'.join(lines) + '\n')


def portraits(record, out):
    tims = next(t for t in archives(record, 22) if len(t) == 22)
    sheet = Image.new('RGB', (7 * PORT_W, 3 * PORT_H))
    sil = silhouette(tims[21])
    words = [word(0, 0, 0), word(6, 6, 8)] + [0] * (PORT_COLOURS - 2)
    (out / 'PORT21.BIN').write_bytes(struct.pack('>2H', word(255, 255, 255), 0) +
        struct.pack(f'>{PORT_COLOURS}H', *words) +
        np.pad(sil, ((0, 0), (0, PORT_STRIDE - PORT_W))).tobytes())
    for ident in range(21):
        image = portrait(tims[ident])
        idx, words = quantise(image, PORT_COLOURS, True)
        (out / f'PORT{ident:02}.BIN').write_bytes(struct.pack('>2H', glow_colour(image), 0) +
                                                  struct.pack(f'>{PORT_COLOURS}H', *words) +
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
    with tempfile.TemporaryDirectory() as tmp:
        names(args.select, out, tmp)
    texts(args.exe.read_bytes(), out)
    layout.update(faces(args.exe.read_bytes(), out))
    lines = ['/* Generated by scripts/pack_menu_art.py: where the baked art puts things. */',
             f'enum {{ MENU_BG_W={W}, MENU_BG_H={H}, MENU_PORT_W={PORT_W}, MENU_PORT_H={PORT_H},']
    lines += [f'       {k}={v},' for k, v in layout.items()]
    lines.append('       MENU_LAYOUT_END };')
    (out / 'menu_layout.h').write_text('\n'.join(lines) + '\n')
    print(layout)


if __name__ == '__main__':
    main()
