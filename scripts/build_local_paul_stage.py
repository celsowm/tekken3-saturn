#!/usr/bin/env python3
"""Convert private USA stg_a ARC/TMD into a Saturn NBG0 panorama and RBG0 tile.

Only user-supplied files are read. No retail data is shipped with this tool.
The panorama becomes a 360-degree cylinder picture baked from above the
fighters' midpoint (panorama_raycast.py); src/game/stage_backdrop.c maps it
onto the screen line by line. Floor distance shading is not emulated.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from psx_character_textures import upload_block

# The backdrop cylinder: an 8-bit 1024 x 256 NBG0 bitmap whose rows 240-255
# stay free for the VDP2 tables (stage_vdp2.c). The tangent range covers the
# fight camera's pitches and eye heights (tools/psx_match_camera_eval.c).
COLUMNS, ROWS = 1024, 240
BAKE_HEIGHT = 1100
TAN_TOP, ROWS_PER_TAN = 0.80, 200.0
US_EXE_SHA = 'fbda8b68e5799dbef4af39a161783bc670c15b0aa0e87dce65e210717da19b8c'
PAUL_ARC_SHA = '7e26ca6f72af5d38164ed47a092338847aea5ec2c0e2216601dd2b5b0b2e9e56'
PAUL_TMD_SHA = '6b0f2c413ca02c5a47182733b1f37afe4f0eec28c545a7fc0520b222351c382b'


def read_arc(arc):
    if len(arc) < 24 or struct.unpack_from('<I', arc)[0] != 2:
        raise ValueError('expected two-member stage ARC')
    first, size, floor, floor_size = struct.unpack_from('<4I', arc, 4)
    if first != 24 or first + size != floor or floor_size != 808 or floor + floor_size != len(arc):
        raise ValueError('invalid stage ARC extents')
    vram = [0] * (1024 * 512)
    at, count = first, 0
    while at + 8 <= floor and struct.unpack_from('<I', arc, at)[0] == 0x10:
        if struct.unpack_from('<I', arc, at + 4)[0] != 8:
            raise ValueError('stage requires 4-bit TIMs with a CLUT')
        at = upload_block(vram, arc, at + 8, 0, 0)
        at = upload_block(vram, arc, at, 0, 0)
        count += 1
    if not count or at != floor - 4 or arc[at:floor] != bytes(4):
        raise ValueError('invalid stage TIM sequence')
    return vram, arc[floor:], count


def texel(vram, page, clut, u, v):
    if (page >> 7) & 3 or page & ~0x1ff or clut & 0x8000:
        raise ValueError('invalid 4-bit stage material')
    u, v = int(u) & 255, int(v) & 255
    word = vram[(((page >> 4) & 1) * 256 + v) * 1024 + (page & 15) * 64 + u // 4]
    index = word >> ((u & 3) * 4) & 15
    return vram[((clut >> 6) & 511) * 1024 + (clut & 63) * 16 + index]


def floor_tile(vram, descriptor):
    if len(descriptor) != 808 or struct.unpack_from('<I', descriptor)[0] != 128:
        raise ValueError('invalid floor descriptor')
    entries = [struct.unpack_from('<II', descriptor, 4 + i * 8) for i in range(100)]
    if len(set(entries)) != 1 or struct.unpack_from('<h', descriptor, 804)[0] != 0:
        raise ValueError('Paul stage requires one repeating standard floor tile')
    w0, w1 = entries[0]
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
    palette = sorted(set(colors))
    if len(palette) > 16:
        raise ValueError('floor palette exceeds 16 colors')
    indices = [palette.index(c) for c in colors]
    # Cell order, then row order. Saturn's leftmost dot is the high nibble.
    packed = bytearray()
    for cy in range(8):
        for cx in range(8):
            for y in range(8):
                for x in range(0, 8, 2):
                    at = (cy * 8 + y) * 64 + cx * 8 + x
                    packed.append(indices[at] << 4 | indices[at + 1])
    palette += [0] * (16 - len(palette))
    return bytes(packed), palette, colors


def floor_shade_corrections(tile, palette):
    """Best additive RGB offsets for each PS1 level, weighted by tile texels."""
    counts = [0] * 16
    for byte in tile:
        counts[byte >> 4] += 1
        counts[byte & 15] += 1
    result = bytearray()
    for level in range(255):
        for shift in (0, 5, 10):
            channels = [(value >> shift) & 31 for value in palette]
            targets = [min(31, int(value * level / 128 + 0.5)) for value in channels]
            def cost(delta):
                return sum(count * abs(max(0, min(31, value + delta)) - target)
                           for count, value, target in zip(counts, channels, targets))
            delta = min(range(-16, 16), key=lambda d: (cost(d), abs(d)))
            result.append(delta & 255)
    return bytes(result)


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


def cylinder(tmd, vram, height):
    """The panorama as seen from BAKE_HEIGHT above the midpoint, one 360-degree
    picture: column c looks along atan2(x, z) = 2 pi (c + 0.5) / COLUMNS, row r
    at the elevation tangent TAN_TOP - (r + 0.5) / ROWS_PER_TAN."""
    import numpy as np
    from panorama_raycast import Panorama, cylinder_dirs
    scene = Panorama(tmd, vram, height)
    dirs = cylinder_dirs(COLUMNS, ROWS, TAN_TOP, ROWS_PER_TAN)
    _, colour, _ = scene.cast((0.0, -float(BAKE_HEIGHT), 0.0), dirs)
    opaque = colour != 0
    distinct = sorted(set(int(c) for c in colour[opaque]))
    if len(distinct) <= 255:
        palette = distinct
        lookup = {c: i for i, c in enumerate(palette)}
        chosen = np.array([lookup[int(c)] for c in colour[opaque]])
    else:
        # One 8-bit CRAM bank: median cut with k-means refinement, no dither
        # (the layer is magnified about 2.6 times, so patterns would show).
        from PIL import Image
        rgb = np.stack([(colour[opaque] >> (5*k)) & 31 for k in range(3)], axis=1).astype(np.uint8)*8
        strip = Image.fromarray(rgb.reshape(1, -1, 3), 'RGB')
        quantized = strip.quantize(colors=255, method=Image.Quantize.MEDIANCUT, kmeans=8,
                                   dither=Image.Dither.NONE)
        levels = np.array(quantized.getpalette()[:255*3]).reshape(-1, 3)//8
        palette = [int(0x8000 | r | g << 5 | b << 10) for r, g, b in levels]
        chosen = np.array(quantized).reshape(-1)
    indices = np.zeros(len(colour), dtype=np.uint8)
    indices[opaque] = chosen + 1
    # Largest channel error in 5-bit steps, for the manifest.
    got = np.array(palette)[chosen]
    error = max(int(np.abs(((got >> (5*k)) & 31) - ((colour[opaque] >> (5*k)) & 31)).max()) for k in range(3))
    return bytes(indices), [0] + palette, scene, dict(distinct=len(distinct), max_channel_error=error)

def build(source, output):
    arc_path, tmd_path, exe_path = source/'raw/036.bin', source/'raw/056.bin', source/'SLUS_004.02'
    arc, tmd, exe = arc_path.read_bytes(), tmd_path.read_bytes(), exe_path.read_bytes()
    if hashlib.sha256(exe).hexdigest() != US_EXE_SHA:
        raise ValueError('stage metadata requires the qualified USA executable')
    if hashlib.sha256(arc).hexdigest() != PAUL_ARC_SHA or hashlib.sha256(tmd).hexdigest() != PAUL_TMD_SHA:
        raise ValueError('expected the original Paul stg_a ARC/TMD pair')
    # USA table, independently located from the documented stage-height array.
    height = struct.unpack_from('<i', exe, 0x800 + 0x25138 - 0x10000)[0]
    vram, descriptor, tims = read_arc(arc)
    tile, palette, colors = floor_tile(vram, descriptor)
    # FloorDrawGrid table: the USA layout maps the documented JP address
    # 0x8001df6c identically; the executable hash above fixes the version.
    fade = exe[0x800 + 0xdf6c:0x800 + 0xdf6c + 1024]
    if len(fade) != 1024:
        raise ValueError('truncated original floor fade table')
    corrections = floor_shade_corrections(tile, palette)
    picture, picture_palette, scene, quantization = cylinder(tmd, vram, height)
    output.mkdir(parents=True, exist_ok=True)
    # Whole-sector sections avoid restarting unaligned CD reads for each chunk.
    sections = dict(tile=2048, floor_palette=4096, picture_palette=6144, picture=8192)
    payload = struct.pack('>16I', 0x54335354, 2, COLUMNS, ROWS, 64, 64, len(tile), 16,
                          sections['tile'], sections['floor_palette'], sections['picture_palette'],
                          len(picture_palette), sections['picture'], len(picture), 0x46414445, 0x53515254)
    payload += bytes(sections['tile']-len(payload)) + tile
    payload += bytes(sections['floor_palette']-len(payload)) + struct.pack('>16H', *palette)
    payload += fade + corrections
    payload += bytes(sections['picture_palette']-len(payload)) + struct.pack(f'>{len(picture_palette)}H', *picture_palette)
    payload += bytes(sections['picture']-len(payload)) + picture
    payload += bytes(-len(payload) % 2048)
    # SquareRoot0 can read before its table on overflow. This window contains
    # private executable bytes and is used only by the experimental floor.
    sqrt_offset = 0x800 + 0x3af40
    before_sqrt = exe[sqrt_offset-640:sqrt_offset]
    if len(before_sqrt) != 640:
        raise ValueError('truncated SquareRoot0 preceding window')
    payload += before_sqrt + bytes(2048-len(before_sqrt))
    (output/'PAUL.STG').write_bytes(payload)
    half = round(scene.half)
    (output/'t3_local_stage.h').write_text('\n'.join([
        '#ifndef T3_LOCAL_STAGE_H', '#define T3_LOCAL_STAGE_H',
        f'#define T3_STAGE_FILE_BYTES {len(payload)}u',
        f'#define T3_STAGE_COLUMNS {COLUMNS}u', f'#define T3_STAGE_ROWS {ROWS}u',
        f'#define T3_STAGE_PICTURE_COLOURS {len(picture_palette)}u',
        f'#define T3_STAGE_BOX_HALF {half}', f'#define T3_STAGE_BAKE_HEIGHT {BAKE_HEIGHT}',
        f'#define T3_STAGE_TAN_TOP {round(TAN_TOP*65536)}',
        f'#define T3_STAGE_ROWS_PER_TAN {round(ROWS_PER_TAN*65536)}',
        '#endif', '']))
    manifest = dict(stage='Paul / stg_a', format_version=2, tims=tims, panorama_height=height,
                    panorama_half=half, cylinder=dict(columns=COLUMNS, rows=ROWS, bake_height=BAKE_HEIGHT,
                                                      tan_top=TAN_TOP, rows_per_tan=ROWS_PER_TAN,
                                                      colours=len(picture_palette)-1, **quantization),
                    floor_shading=struct.unpack_from('<h', descriptor, 806)[0],
                    floor='Original oriented 64x64 repeating tile; distance shading is not reproduced.',
                    sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (arc_path,tmd_path,exe_path)},
                    output_sha256=hashlib.sha256(payload).hexdigest(), output_bytes=len(payload))
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    try:
        from PIL import Image
        def rgb(c): return tuple(((c >> (5*k)) & 31)*255//31 for k in range(3))
        im=Image.new('RGB',(COLUMNS,ROWS));im.putdata([rgb(picture_palette[i]) for i in picture]);im.save(output/'panorama.png')
        im=Image.new('RGB',(64,64));im.putdata([rgb(c) for c in colors]);im.save(output/'floor.png')
    except ImportError:
        pass
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();build(a.source.resolve(),a.output.resolve())
