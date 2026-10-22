#!/usr/bin/env python3
"""Convert private USA stg_a ARC/TMD into a Saturn NBG0 panorama and RBG0 tile.

Only user-supplied files are read. No retail data is shipped with this tool.
The panorama is a fixed-bearing perspective bake for this port's camera;
original dynamic panorama parallax and floor distance shading are not emulated.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from psx_character_textures import upload_block

WIDTH, HEIGHT, STRIDE = 320, 224, 512
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


def panorama(tmd, vram, height):
    pixels = [0x8000] * (STRIDE * HEIGHT)
    scale = 96 / 65536
    ey, ez = 2.0, -5.6
    length = math.hypot(.5, 5.6)
    sy, cz = -.5 / length, 5.6 / length
    focal = HEIGHT / 2 / math.tan(math.radians(22.5))
    def view(v):
        (x, y, z), (u, vv) = v
        wx, wy, wz = x * 8 * scale, -(y * 8 + height) * scale, -z * 8 * scale
        return [-wx, cz * (wy - ey) - sy * (wz - ez), sy * (wy - ey) + cz * (wz - ez), u, vv]
    def clipped(poly):
        out = []
        for a, b in zip(poly, poly[1:] + poly[:1]):
            if a[2] >= .25:
                out.append(a)
            if (a[2] >= .25) != (b[2] >= .25):
                t = (.25 - a[2]) / (b[2] - a[2])
                out.append([a[i] + t * (b[i] - a[i]) for i in range(5)])
        return out
    def triangle(points, rgb, material):
        p = [(WIDTH/2 + focal*v[0]/v[2], HEIGHT/2 - focal*v[1]/v[2], v[3], v[4]) for v in points]
        (ax, ay, au, av), (bx, by, bu, bv), (cx, cy, cu, cv) = p
        det = (by-cy)*(ax-cx)+(cx-bx)*(ay-cy)
        if abs(det) < 1e-9:
            return
        for y in range(max(0, math.ceil(min(ay, by, cy))), min(HEIGHT-1, math.floor(max(ay, by, cy)))+1):
            for x in range(max(0, math.ceil(min(ax, bx, cx))), min(WIDTH-1, math.floor(max(ax, bx, cx)))+1):
                a = ((by-cy)*(x-cx)+(cx-bx)*(y-cy))/det
                b = ((cy-ay)*(x-cx)+(ax-cx)*(y-cy))/det
                c = 1-a-b
                if min(a, b, c) < -1e-7:
                    continue
                if material:
                    color = texel(vram, *material, round(a*au+b*bu+c*cu), round(a*av+b*bv+c*cv))
                    if not color:
                        continue
                    channels = [min(31, ((color >> (5*k)) & 31) * rgb[k] // 128) for k in range(3)]
                else:
                    channels = [n >> 3 for n in rgb]
                pixels[y*STRIDE+x] = 0x8000 | sum(n << (5*k) for k, n in enumerate(channels))
    # The original links every panorama packet into one OT bucket (reverse order).
    packets = primitives(tmd)
    for rgb, material, corners in reversed(packets):
        for ids in ([(0, 1, 2)] if len(corners) == 3 else [(0, 1, 2), (1, 3, 2)]):
            poly = clipped([view(corners[i]) for i in ids])
            for i in range(1, len(poly)-1):
                triangle((poly[0], poly[i], poly[i+1]), rgb, material)
    return pixels, len(packets)


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
    background, packets = panorama(tmd, vram, height)
    output.mkdir(parents=True, exist_ok=True)
    # Whole-sector sections avoid restarting unaligned CD reads for each chunk.
    payload = struct.pack('>16I', 0x54335354, 1, STRIDE, HEIGHT, 64, 64, len(tile), 16, 2048, 4096, 6144, len(background)*2, 0, 0, 0, 0)
    payload += bytes(2048-len(payload)) + tile + struct.pack('>16H', *palette) + bytes(2016)
    payload += struct.pack('>'+str(len(background))+'H', *background)
    (output/'PAUL.STG').write_bytes(payload)
    (output/'t3_local_stage.h').write_text('#ifndef T3_LOCAL_STAGE_H\n#define T3_LOCAL_STAGE_H\n#define T3_STAGE_FILE_BYTES '+str(len(payload))+'u\n#endif\n')
    manifest = dict(stage='Paul / stg_a', format_version=1, tims=tims, panorama_packets=packets,
                    panorama_height=height, floor_shading=struct.unpack_from('<h', descriptor, 806)[0],
                    panorama='Fixed-bearing bake; dynamic source parallax is not reproduced.',
                    floor='Original oriented 64x64 repeating tile; distance shading is not reproduced.',
                    sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in (arc_path,tmd_path,exe_path)},
                    output_sha256=hashlib.sha256(payload).hexdigest(), output_bytes=len(payload))
    (output/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    try:
        from PIL import Image
        def rgb(c): return tuple(((c >> (5*k)) & 31)*255//31 for k in range(3))
        im=Image.new('RGB',(STRIDE,HEIGHT));im.putdata([rgb(c) for c in background]);im.crop((0,0,WIDTH,HEIGHT)).save(output/'panorama.png')
        im=Image.new('RGB',(64,64));im.putdata([rgb(c) for c in colors]);im.save(output/'floor.png')
    except ImportError:
        pass
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();build(a.source.resolve(),a.output.resolve())
