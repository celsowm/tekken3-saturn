#!/usr/bin/env python3
"""Convert the private USA stage ARC/TMD pairs into STGnn.BIN disc files.

Only user-supplied files are read. No retail data is shipped with this tool.
Each panorama becomes a 360-degree cylinder picture baked from above the
fighters' midpoint (panorama_raycast.py); src/game/stage_backdrop.c maps it
onto the screen line by line. Each floor becomes one 8-bit bitmap repeating
across the ground. src/game/stage_format.h defines the file layout.

Floor distance shading is not emulated, and a spotlight floor is one fixed
brightness (spot_level), because a VDP2 floor has no per-vertex brightness.
"""
import argparse
from concurrent.futures import ProcessPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import struct
import time

from fighter_roster import STAGE_NUMBERS
from psx_character_textures import upload_block

ROOT = Path(__file__).resolve().parents[1]
FORMAT = {name: value for name, value in re.findall(r'#define T3_STAGE_(\w+) (0x[0-9a-f]+|\d+)',
                                                    (ROOT/'src/game/stage_format.h').read_text())}
COLUMNS, ROWS = int(FORMAT['COLUMNS']), int(FORMAT['ROWS'])
FLOOR_W, FLOOR_H = int(FORMAT['FLOOR_W']), int(FORMAT['FLOOR_H'])
BAKE_HEIGHT = int(FORMAT['BAKE_HEIGHT'])
TAN_TOP, ROWS_PER_TAN = int(FORMAT['TAN_TOP'])/65536, int(FORMAT['ROWS_PER_TAN'])/65536
HEAD_BYTES, FLOOR_PALETTE_AT = int(FORMAT['HEAD_BYTES']), int(FORMAT['FLOOR_PALETTE_AT'])
PICTURE_PALETTE_AT = int(FORMAT['PICTURE_PALETTE_AT'])
# Rays per texel side. VDP2 magnifies the picture about 1.8 x 1.5 with nearest
# sampling, so one ray per texel leaves stair steps and dropped source texels.
SUPERSAMPLE = 4
US_EXE_SHA = 'fbda8b68e5799dbef4af39a161783bc670c15b0aa0e87dce65e210717da19b8c'
LAYOUT = ROOT/'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json'
EXE_BASE, EXE_HEADER = 0x80010000, 0x800
# Stage 7 belongs to costumes 2 and 3 of Xiaoyu and Jin; the game has costume 0 only.
USA_HEIGHTS = 0x8002_5138               # Rev.1 0x80025394
CHARACTER_RECORDS = 0x8009_8120         # Rev.1, four costume pointers per character
STAGE_RECORDS, STAGE_RECORD_BYTES = 0x8009709C, 0x1C
# Stage 11's sky shows through its floor grates (stages.md, "Clear colour").
GRATE_STAGE, GRATE_BACK = 11, (72, 104, 200)
# FUN_80049AE0: grey vertex colour K / r^2 from the light points (stages.md, "Floor kinds").
SPOT_K = {1: 253755392, 2: 800000000}
SPOT_CLAMP = 254
# True Ogre's clamp 0x800AE25C, and the stage where it is lower.
OGRE_CLAMP, OGRE_CLAMP_DIM, OGRE_DIM_STAGE = 192, 128, 13
TILE_UNITS, FIGHTER_GAP = 1800, 1400


def read_arc(arc):
    if len(arc) < 24 or struct.unpack_from('<I', arc)[0] != 2:
        raise ValueError('expected two-member stage ARC')
    first, size, floor, floor_size = struct.unpack_from('<4I', arc, 4)
    if first != 24 or first + size != floor or floor_size != 808 or floor + floor_size != len(arc):
        raise ValueError('invalid stage ARC extents')
    vram = [0] * (1024 * 512)
    at, count = first, 0
    # FUN_8006E664 stops at the first word that is not a TIM ID, so stg_m's tail never loads.
    while at + 8 <= floor and struct.unpack_from('<I', arc, at)[0] == 0x10:
        if struct.unpack_from('<I', arc, at + 4)[0] != 8:
            raise ValueError('stage requires 4-bit TIMs with a CLUT')
        at = upload_block(vram, arc, at + 8, 0, 0)
        at = upload_block(vram, arc, at, 0, 0)
        count += 1
    if not count or at > floor:
        raise ValueError('invalid stage TIM sequence')
    return vram, arc[floor:], count


def texel(vram, page, clut, u, v):
    if (page >> 7) & 3 or page & ~0x1ff or clut & 0x8000:
        raise ValueError('invalid 4-bit stage material')
    u, v = int(u) & 255, int(v) & 255
    word = vram[(((page >> 4) & 1) * 256 + v) * 1024 + (page & 15) * 64 + u // 4]
    index = word >> ((u & 3) * 4) & 15
    return vram[((clut >> 6) & 511) * 1024 + (clut & 63) * 16 + index]


def floor_entry(vram, w0, w1):
    """One tile's 64 x 64 colours, row-major: dot x runs from corner v0 to v1, y from v0 to v2."""
    page, clut = w0 >> 16 & 0x19f, w1 >> 16
    corner_sets = (((0, 0), (1, 0), (0, 1), (1, 1)),
                   ((0, 1), (0, 0), (1, 1), (1, 0)),
                   ((1, 1), (0, 1), (1, 0), (0, 0)),
                   ((1, 0), (1, 1), (0, 0), (0, 1)))
    corners = corner_sets[w0 >> 30]
    flip = w0 >> 25 & 3
    c0, c1, c2 = (corners[k ^ flip] for k in (0, 1, 2))
    colors = []
    for y in range(64):
        for x in range(64):
            u = (w0 & 255) + c0[0] * 63 + (c1[0] - c0[0]) * x + (c2[0] - c0[0]) * y
            v = (w0 >> 8 & 255) + c0[1] * 63 + (c1[1] - c0[1]) * x + (c2[1] - c0[1]) * y
            colors.append(texel(vram, page, clut, u, v))
    return colors


def floor_kind(descriptor):
    return struct.unpack_from('<h', descriptor, 0x324)[0]


def floor_bitmap(vram, descriptor, level=128):
    """The floor as FLOOR_W x FLOOR_H palette indices with up to 255 colours. Index 0 is
    transparent (a texel of value 0, which the GPU leaves undrawn). Map dot (mx, my) is PS1
    (x, -z) * 64 / 1800 (stage_vdp2.c), so FloorDrawGrid's entry column (tx + 5) % 10, row
    (4 - tz) % 10 become (cx + 5) % 10, (cy + 5) % 10. A pattern longer than the bitmap
    repeats sooner than the original's. Returns (indices, palette, whether it was cut)."""
    if len(descriptor) != 808 or struct.unpack_from('<I', descriptor)[0] != 128:
        raise ValueError('invalid floor descriptor')
    entries = [struct.unpack_from('<II', descriptor, 4 + i * 8) for i in range(100)]
    tiles = {}
    columns, rows = FLOOR_W // 64, FLOOR_H // 64
    grid = [[entries[(cy + 5) % 10 * 10 + (cx + 5) % 10] for cx in range(columns)] for cy in range(rows)]
    for row in grid:
        for entry in row:
            if entry not in tiles:
                tiles[entry] = floor_entry(vram, *entry)
    wide = [[entries[r * 10 + c] for c in range(10)] for r in range(10)]
    cut = any(wide[r][c] != wide[r][(c + columns) % 10] or wide[r][c] != wide[(r + rows) % 10][c]
              for r in range(10) for c in range(10))
    colours = sorted({c for tile in tiles.values() for c in tile} - {0})
    if len(colours) > 255:
        raise ValueError('floor exceeds 255 colours')
    lookup = {c: i + 1 for i, c in enumerate(colours)}
    lookup[0] = 0
    indices = bytearray(FLOOR_W * FLOOR_H)
    for cy, row in enumerate(grid):
        for cx, entry in enumerate(row):
            tile = tiles[entry]
            for y in range(64):
                at = (cy * 64 + y) * FLOOR_W + cx * 64
                indices[at:at + 64] = bytes(lookup[c] for c in tile[y * 64:y * 64 + 64])
    return bytes(indices), [0] + [scale_colour(c, level) for c in colours], cut


def scale_colour(colour, level):
    out = colour & 0x8000
    for shift in (0, 5, 10):
        out |= min(31, ((colour >> shift) & 31) * level // 128) << shift
    return out


def spot_level(kind, clamp=None):
    """Mean of FUN_80049AE0's brightness over the floor near the fighters, 128 unchanged.
    Kind 1 lights each fighter, kind 2 their midpoint; both are taken at a typical gap."""
    if kind not in SPOT_K:
        return 128
    lights = ((-FIGHTER_GAP/2, 0), (FIGHTER_GAP/2, 0)) if kind == 1 else ((0, 0),)
    limit = clamp if clamp is not None else 192 if kind == 2 else SPOT_CLAMP
    total, count = 0, 0
    for tz in range(-4, 5):
        for tx in range(-4, 5):
            x, z = tx*TILE_UNITS, tz*TILE_UNITS
            level = sum(SPOT_K[kind]/max(1, (x-lx)**2 + (z-lz)**2) for lx, lz in lights)
            total += min(limit, level)
            count += 1
    return max(1, round(total/count))


def primitives(tmd):
    if len(tmd) < 1020 or struct.unpack_from('<I', tmd)[0] != 0x41 or struct.unpack_from('<I', tmd, 8)[0] != 36:
        raise ValueError('invalid stage TMD header')
    result = []
    for obj in range(36):
        vo, count, _, _, po, faces, _ = struct.unpack_from('<7I', tmd, 12 + 28 * obj)
        if 12 + vo + count * 8 > len(tmd):
            raise ValueError('stage vertices outside TMD')
        vertices = [struct.unpack_from('<3h', tmd, 12 + vo + 8 * v) for v in range(count)]
        at = 12 + po
        for _ in range(faces):
            if at + 4 > len(tmd):
                raise ValueError('truncated stage primitive')
            rgb, code = tmd[at:at+3], tmd[at+3]
            size = {0x24: 20, 0x28: 8, 0x2c: 20}.get(code)
            if not size or at + size > len(tmd):
                raise ValueError('invalid stage primitive')
            if code == 0x28:
                ids, uv, material = tmd[at+4:at+8], [(0, 0)] * 4, None
            else:
                ids = tmd[at+16:at+(19 if code == 0x24 else 20)]
                uv = [(tmd[at+i], tmd[at+i+1]) for i in (4, 8, 12)]
                if code == 0x2c:
                    uv.append((tmd[at+14], tmd[at+15]))
                material = struct.unpack_from('<H', tmd, at+10)[0], struct.unpack_from('<H', tmd, at+6)[0]
            if any(i >= count for i in ids):
                raise ValueError('stage vertex index outside object')
            result.append((rgb, material, [(vertices[i], uv[k]) for k, i in enumerate(ids)]))
            at += size
    return result


def box_filter(colour, columns, rows, factor):
    """RGB555 mean of each factor x factor block of samples, 0 for transparent.
    A texel is opaque when at least half its samples are, and transparent samples
    stay out of the mean so that no edge fades towards black."""
    import numpy as np
    seen = (colour != 0).reshape(rows, factor, columns, factor)
    count = seen.sum(axis=(1, 3))
    out = np.zeros((rows, columns), dtype=np.int64)
    for k in range(3):
        channel = ((colour >> (5*k)) & 31).reshape(rows, factor, columns, factor)
        out |= np.rint((channel*seen).sum(axis=(1, 3))/np.maximum(count, 1)).astype(np.int64) << (5*k)
    return np.where(count*2 >= factor*factor, out | 0x8000, 0).reshape(-1)


def cylinder(tmd, vram, height, supersample=SUPERSAMPLE):
    """The panorama as seen from BAKE_HEIGHT above the midpoint, one 360-degree
    picture: column c looks along atan2(x, z) = 2 pi (c + 0.5) / COLUMNS, row r
    at the elevation tangent TAN_TOP - (r + 0.5) / ROWS_PER_TAN, each texel the
    mean of supersample x supersample rays centred there."""
    import numpy as np
    from panorama_raycast import Panorama, cylinder_dirs
    scene = Panorama(tmd, vram, height)
    dirs = cylinder_dirs(COLUMNS*supersample, ROWS*supersample, TAN_TOP, ROWS_PER_TAN*supersample)
    # In bands of rows: every triangle tests every ray of a cast.
    band = COLUMNS*supersample*ROWS*supersample//24
    colour = np.concatenate([scene.cast((0.0, -float(BAKE_HEIGHT), 0.0), dirs[at:at + band])[1]
                             for at in range(0, len(dirs), band)])
    colour = box_filter(colour, COLUMNS, ROWS, supersample)
    opaque = colour != 0
    distinct = sorted(set(int(c) for c in colour[opaque]))
    if len(distinct) <= 255:
        palette = distinct
        lookup = {c: i for i, c in enumerate(palette)}
        chosen = np.array([lookup[int(c)] for c in colour[opaque]])
    else:
        # One 8-bit CRAM bank: median cut with k-means refinement, no dither
        # (the layer is magnified on screen, so patterns would show).
        from PIL import Image
        rgb = np.stack([(colour[opaque] >> (5*k)) & 31 for k in range(3)], axis=1).astype(np.uint8)*8
        strip = Image.fromarray(rgb.reshape(1, -1, 3), 'RGB')
        quantized = strip.quantize(colors=255, method=Image.Quantize.MEDIANCUT, kmeans=8,
                                   dither=Image.Dither.NONE)
        levels = np.array(quantized.getpalette()[:255*3]).reshape(-1, 3)
        levels = np.minimum(31, (levels + 4)//8)
        palette = [int(0x8000 | r | g << 5 | b << 10) for r, g, b in levels]
        chosen = np.array(quantized).reshape(-1)
    indices = np.zeros(len(colour), dtype=np.uint8)
    indices[opaque] = chosen + 1
    # Largest channel error in 5-bit steps, for the manifest.
    got = np.array(palette)[chosen]
    error = max(int(np.abs(((got >> (5*k)) & 31) - ((colour[opaque] >> (5*k)) & 31)).max()) for k in range(3))
    # Whether the picture meets the top or bottom of its range: a wall cut off there.
    edges = dict(top_row_opaque=round(float(opaque[:COLUMNS].mean()), 3),
                 bottom_row_opaque=round(float(opaque[-COLUMNS:].mean()), 3),
                 opaque=round(float(opaque.mean()), 3))
    return bytes(indices), [0] + palette, scene, dict(distinct=len(distinct), max_channel_error=error, **edges)


def rgb555(red, green, blue):
    return 0x8000 | red >> 3 | (green >> 3) << 5 | (blue >> 3) << 10


def usa_file_offset(address):
    """Where a Rev.1 address lies in the USA executable file."""
    offset = address - EXE_BASE
    for start, length, source in json.loads(LAYOUT.read_text())['blocks']['exe']['segments']:
        if start <= offset < start + length:
            return EXE_HEADER + source + offset - start
    raise ValueError(f'{address:#x} is not in the USA layout')


def stage_metadata(exe, stage):
    """The stage's panorama height and shadow colour, read from the USA executable."""
    height = struct.unpack_from('<i', exe, EXE_HEADER + USA_HEIGHTS - EXE_BASE + 4*stage)[0]
    if not 200 <= height <= 600:
        raise ValueError(f'stage {stage}: implausible panorama height {height}')
    shadow = exe[usa_file_offset(STAGE_RECORDS + STAGE_RECORD_BYTES*stage) + 0x10:][:3]
    return height, tuple(shadow)


def fighter_home(exe, character):
    """A character's (home stage, music track) in its first costume."""
    pointer = struct.unpack_from('<I', exe, usa_file_offset(CHARACTER_RECORDS + 16*character))[0]
    record = EXE_HEADER + pointer - EXE_BASE
    return exe[record + 0xa], exe[record + 0xb]


def pack(number, box_half, back, shadow, ogre_level, floor_palette, floor, picture_palette, picture):
    words = [int(FORMAT['MAGIC'], 16), int(FORMAT['VERSION']), number, box_half, back, shadow, ogre_level]
    payload = struct.pack(f'>{len(words)}I', *words)
    payload += bytes(FLOOR_PALETTE_AT-len(payload))
    payload += struct.pack(f'>{len(floor_palette)}H', *floor_palette)
    payload += bytes(PICTURE_PALETTE_AT-len(payload))
    payload += struct.pack(f'>{len(picture_palette)}H', *picture_palette)
    payload += bytes(HEAD_BYTES-len(payload))
    if len(floor) != FLOOR_W*FLOOR_H or len(picture) != COLUMNS*ROWS:
        raise ValueError('unexpected floor or picture size')
    return payload + floor + picture


def build_stage(source, output, stage, supersample=SUPERSAMPLE):
    started = time.monotonic()
    arc_path, tmd_path = source/f'raw/{36 + stage:03}.bin', source/f'raw/{56 + stage:03}.bin'
    arc, tmd, exe = arc_path.read_bytes(), tmd_path.read_bytes(), (source/'SLUS_004.02').read_bytes()
    height, shadow = stage_metadata(exe, stage)
    vram, descriptor, tims = read_arc(arc)
    kind = floor_kind(descriptor)
    level = spot_level(kind)
    floor, floor_palette, cut = floor_bitmap(vram, descriptor, level)
    picture, picture_palette, scene, quantization = cylinder(tmd, vram, height, supersample)
    half = round(scene.half)
    back = rgb555(*GRATE_BACK) if stage == GRATE_STAGE else 0x8000
    # True Ogre turns a standard floor into kind 2; a spotlight floor keeps its own.
    ogre_level = 128 if kind else spot_level(2, OGRE_CLAMP_DIM if stage == OGRE_DIM_STAGE else OGRE_CLAMP)
    payload = pack(stage, half, back, rgb555(*shadow), ogre_level, floor_palette, floor, picture_palette, picture)
    (output/f'STG{stage:02}.BIN').write_bytes(payload)
    manifest = dict(stage=stage, tims=tims, panorama_height=height, panorama_half=half, shadow=shadow,
                    floor_kind=kind, floor_shading=struct.unpack_from('<h', descriptor, 0x326)[0], floor_level=level,
                    ogre_level=ogre_level, floor_colours=len(floor_palette)-1, floor_pattern_cut=cut,
                    cylinder=dict(supersample=supersample, colours=len(picture_palette)-1, **quantization),
                    sha256={p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (arc_path, tmd_path)},
                    output_sha256=hashlib.sha256(payload).hexdigest(), output_bytes=len(payload),
                    bake_seconds=round(time.monotonic()-started, 1))
    (output/f'STG{stage:02}.json').write_text(json.dumps(manifest, indent=2)+'\n')
    preview(output, stage, picture, picture_palette, floor, floor_palette)
    return manifest


def preview(output, stage, picture, picture_palette, floor, floor_palette):
    try:
        from PIL import Image
    except ImportError:
        return

    def rgb(c):
        return tuple(((c >> (5*k)) & 31)*255//31 for k in range(3))
    im = Image.new('RGB', (COLUMNS, ROWS))
    im.putdata([rgb(picture_palette[i]) for i in picture])
    im.save(output/f'panorama{stage:02}.png')
    im = Image.new('RGB', (FLOOR_W, FLOOR_H))
    im.putdata([rgb(floor_palette[i]) for i in floor])
    im.save(output/f'floor{stage:02}.png')


def contact_sheet(output, stages):
    """Every panorama beside its floor, for looking at the whole set at once."""
    from PIL import Image
    row = ROWS+8
    sheet = Image.new('RGB', (COLUMNS+FLOOR_W//2+8, row*len(stages)), (255, 0, 255))
    for i, stage in enumerate(stages):
        sheet.paste(Image.open(output/f'panorama{stage:02}.png'), (0, i*row))
        floor = Image.open(output/f'floor{stage:02}.png').resize((FLOOR_W//2, FLOOR_H//2))
        sheet.paste(floor, (COLUMNS+8, i*row))
    sheet.save(output/'contact.png')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True, help='private import with raw/ and SLUS_004.02')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--stages', default=','.join(map(str, STAGE_NUMBERS)))
    p.add_argument('--supersample', type=int, default=SUPERSAMPLE)
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--contact-sheet', action='store_true')
    a = p.parse_args()
    source, output = a.source.resolve(), a.output.resolve()
    if hashlib.sha256((source/'SLUS_004.02').read_bytes()).hexdigest() != US_EXE_SHA:
        raise ValueError('stage metadata requires the qualified USA executable')
    output.mkdir(parents=True, exist_ok=True)
    stages = [int(s) for s in a.stages.split(',')]
    with ProcessPoolExecutor(a.jobs) as pool:
        manifests = list(pool.map(build_stage, [source]*len(stages), [output]*len(stages), stages,
                                  [a.supersample]*len(stages)))
    # The build bakes one stage per step, side by side: only a whole batch writes the summary.
    if len(stages) > 1:
        (output/'stages.json').write_text(json.dumps(manifests, indent=2)+'\n')
    if a.contact_sheet:
        contact_sheet(output, stages)
    for m in manifests:
        print(f"stage {m['stage']:2}: kind {m['floor_kind']} level {m['floor_level']:3} ogre {m['ogre_level']:3} "
              f"floor colours {m['floor_colours']:3} cut {int(m['floor_pattern_cut'])} half {m['panorama_half']:5} "
              f"height {m['panorama_height']} shadow {m['shadow']} top {m['cylinder']['top_row_opaque']:.2f} "
              f"bottom {m['cylinder']['bottom_row_opaque']:.2f} {m['bake_seconds']} s")


if __name__ == '__main__':
    main()
