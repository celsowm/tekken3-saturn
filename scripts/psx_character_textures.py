"""Private character ARC/TIM upload and per-face UV baking, no retail bytes.

Implements the documented P1 VRAM placement. Nested costume directories and
24-bit TIMs are rejected rather than rendered with guessed placement.
"""
import struct


def upload_block(vram, data, at, dx, dy):
    if at + 12 > len(data):
        raise ValueError('truncated TIM block header')
    length, x, y, width, height = struct.unpack_from('<I4H', data, at)
    if length != 12 + width * height * 2 or at + length > len(data):
        raise ValueError('invalid TIM block length')
    x, y = x + dx, y + dy
    if x + width > 1024 or y + height > 512:
        raise ValueError('TIM upload crosses PS1 VRAM boundary')
    for row in range(height):
        words = struct.unpack_from('<' + 'H' * width, data, at + 12 + row * width * 2)
        vram[(y+row)*1024+x:(y+row)*1024+x+width] = words
    return at + length


def load_character_vram(arc):
    if len(arc) < 44 or struct.unpack_from('<I', arc)[0] != 5:
        raise ValueError('expected five-member character ARC')
    ranges = [struct.unpack_from('<II', arc, 4+8*i) for i in range(5)]
    if any(start < 44 or start + size > len(arc) for start, size in ranges):
        raise ValueError('character ARC member outside file')
    spans = sorted((start, start+size) for start, size in ranges if size)
    if any(a[1] > b[0] for a, b in zip(spans, spans[1:])):
        raise ValueError('overlapping character ARC members')
    start, size = ranges[0]
    data, at, count = arc[start:start+size], 0, 0
    vram = [0] * (1024*512)
    while at < len(data):
        if data[at:] == bytes(4):
            break
        if at+8 > len(data):
            raise ValueError('truncated TIM sequence')
        magic, flags = struct.unpack_from('<II', data, at)
        if magic != 0x10 or flags & ~0xB or flags & 7 > 2:
            raise ValueError('unsupported character TIM/directory format')
        at += 8
        if flags & 8:
            at = upload_block(vram, data, at, 0, 504)
        at = upload_block(vram, data, at, 384, 0)
        count += 1
    if not count:
        raise ValueError('empty character texture sequence')
    return vram, count


def sample(vram, material, u, v):
    page, clut = material >> 8, material & 255
    mode = page >> 7 & 3
    if mode not in (0, 1):
        raise ValueError('unsupported character material texture depth')
    u, v = int(u) & 255, int(v) & 255
    per_word = 4 if mode == 0 else 2
    word = vram[v*1024 + 384 + (page & 1)*64 + u//per_word]
    index = word >> ((u % per_word) * (4 if mode == 0 else 8)) & (15 if mode == 0 else 255)
    x, y = 16*(clut & 63)+index, 504+(clut >> 6)
    if x >= 1024 or y >= 512:
        raise ValueError('material CLUT crosses VRAM boundary')
    color = vram[y*1024+x]
    return ((color & 31)*255//31, (color >> 5 & 31)*255//31,
            (color >> 10 & 31)*255//31, 255 if color else 0)


def bake_face(vram, material, uv, width=8, height=8):
    if len(uv) == 3:
        corners = (uv[0], uv[1], uv[2], uv[2])
    elif len(uv) == 4:
        corners = (uv[0], uv[1], uv[3], uv[2])  # strip -> perimeter
    else:
        raise ValueError('texture bake requires a triangle or quad')
    pixels = []
    for y in range(height):
        fy = y / (height-1) if height > 1 else 0
        for x in range(width):
            fx = x / (width-1) if width > 1 else 0
            weights = ((1-fx)*(1-fy), fx*(1-fy), fx*fy, (1-fx)*fy)
            u, v = (round(sum(p[axis]*w for p, w in zip(corners, weights))) for axis in range(2))
            pixels.append(sample(vram, material, u, v))
    return pixels
