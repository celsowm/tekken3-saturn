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


def nested_costume(data, costume):
    """The TIM sequence of one child of a nested costume directory (the Tekken Force enemy slots
    47-50): `u32 4`, four `<offset, size>` pairs, then the children. The game uploads all four at
    16 words and one CLUT row apart and adds 64 to each u and 0x40 to each CLUT field; the child
    alone at the base position is the same picture for the mesh's own coordinates."""
    if costume is None or not 0 <= costume < 4:
        raise ValueError('a nested costume directory needs a costume 0..3')
    start, size = struct.unpack_from('<II', data, 4 + 8 * costume)
    if start + size > len(data):
        raise ValueError('nested costume child outside its member')
    return data[start:start+size]


def load_character_vram(arc, costume=None):
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
    if len(data) >= 36 and struct.unpack_from('<I', data)[0] == 4 and struct.unpack_from('<I', data, 36)[0] == 0xFFFFFFFF:
        data = nested_costume(data, costume)
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
        # A CLUT-only TIM (Gon's ARCs 241 and 245) has a length word and no image.
        if struct.unpack_from('<I', data, at)[0] == 4:
            at += 4
            continue
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


# Native LUT4 bake: each face keeps its texels at the source density and its
# own 16-colour table, as the PS1 draws it (3dmk-models.md, "Texture block").

def corners_of(uv):
    """Face corners in VDP1 order A, B, C, D (PS1 quads are strips; a triangle repeats C)."""
    if len(uv) == 3:
        return (uv[0], uv[1], uv[2], uv[2])
    if len(uv) == 4:
        return (uv[0], uv[1], uv[3], uv[2])
    raise ValueError('texture bake requires a triangle or quad')


def native_size(uv, cap):
    """Patch width and height that keep one texel per source texel along the longer opposite edges."""
    a, b, c, d = corners_of(uv)
    span = lambda p, q: max(abs(p[0]-q[0]), abs(p[1]-q[1]))
    width = max(span(a, b), span(d, c)) + 1
    height = max(span(a, d), span(b, c)) + 1
    width = min(cap, (width + 7) // 8 * 8)
    return max(8, width), max(1, min(cap, height))


def sample_code(vram, material, u, v):
    """The texel's CLUT index and the CLUT's VRAM position, without resolving the colour."""
    page, clut = material >> 8, material & 255
    mode = page >> 7 & 3
    if mode not in (0, 1):
        raise ValueError('unsupported character material texture depth')
    u, v = int(u) & 255, int(v) & 255
    per_word = 4 if mode == 0 else 2
    word = vram[v*1024 + 384 + (page & 1)*64 + u//per_word]
    index = word >> ((u % per_word) * (4 if mode == 0 else 8)) & (15 if mode == 0 else 255)
    return index, 16*(clut & 63), 504+(clut >> 6), mode


def saturn_colour(ps1):
    """PS1 BGR555 (0 = transparent) to Saturn RGB555 with the RGB bit; both keep red in the low bits."""
    return 0 if ps1 == 0 else 0x8000 | (ps1 & 0x7FFF)


def nearest(colour, table):
    r, g, b = colour & 31, colour >> 5 & 31, colour >> 10 & 31
    return min(range(len(table)), key=lambda i: (r-(table[i] & 31))**2 + (g-(table[i] >> 5 & 31))**2 +
               (b-(table[i] >> 10 & 31))**2 if table[i] else 1 << 30)


def reduce_colours(colours, count):
    """Median cut of opaque RGB555 colours (with multiplicity) to at most `count` representatives."""
    boxes = [list(colours)]
    while len(boxes) < count:
        boxes.sort(key=len)
        box = boxes.pop()
        channels = [[c >> s & 31 for c in box] for s in (0, 5, 10)]
        spread = [max(ch) - min(ch) for ch in channels]
        if max(spread) == 0:
            boxes.append(box)
            break
        shift = (0, 5, 10)[spread.index(max(spread))]
        box.sort(key=lambda c: c >> shift & 31)
        boxes += [box[:len(box)//2], box[len(box)//2:]]
    return [0x8000 | round(sum(c & 31 for c in box)/len(box)) | round(sum(c >> 5 & 31 for c in box)/len(box)) << 5 |
            round(sum(c >> 10 & 31 for c in box)/len(box)) << 10 for box in boxes if box]


def bake_face_lut4(vram, material, uv, cap=32):
    """One face as (width, height, codes, lut16, opaque, reduced).

    Codes are 4-bit indices into lut16 (Saturn RGB555). Code 0 is always the
    transparent code: the fight draws every face with VDP1's transparent code 0.
    `opaque` says the face shows no transparent texel. 4-bit faces keep their
    CLUT's own indices; 8-bit faces (and a 4-bit face showing all 16 colours)
    get a table of the colours they show, median-cut to 15 when they show more
    (`reduced` tells)."""
    width, height = native_size(uv, cap)
    corners = corners_of(uv)
    texels = []
    for y in range(height):
        fy = y / (height-1) if height > 1 else 0
        for x in range(width):
            fx = x / (width-1) if width > 1 else 0
            weights = ((1-fx)*(1-fy), fx*(1-fy), fx*fy, (1-fx)*fy)
            u, v = (round(sum(p[axis]*w for p, w in zip(corners, weights))) for axis in range(2))
            texels.append(sample_code(vram, material, u, v))
    _, cx, cy, mode = texels[0]
    if cx + (16 if mode == 0 else 256) > 1024 or cy >= 512:
        raise ValueError('material CLUT crosses VRAM boundary')
    shown = [saturn_colour(vram[cy*1024 + cx + i]) for i, *_ in texels]
    transparent = not all(shown)
    codes = None
    if mode == 0:
        # Keep the CLUT's own order (tables stay shared between faces); entry 0 must be the
        # transparent code, so an opaque colour there moves to an entry this face does not show.
        lut = [saturn_colour(vram[cy*1024 + cx + i]) for i in range(16)]
        codes = [i if lut[i] else 0 for i, *_ in texels]
        if lut[0]:
            free = [j for j in range(1, 16) if j not in codes]
            if 0 in codes and not free:
                codes = None
            else:
                if 0 in codes:
                    lut[free[0]] = lut[0]
                    codes = [free[0] if k == 0 else k for k in codes]
                lut[0] = 0
    reduced = False
    if codes is None:
        opaque = sorted({c for c in shown if c})
        if len(opaque) > 15:
            opaque, reduced = reduce_colours([c for c in shown if c], 15), True
        lut = [0] + opaque + [0] * (15 - len(opaque))
        index = {c: lut.index(c) for c in set(lut) if c}
        codes = [0 if not c else index[c] if c in index else nearest(c, lut) for c in shown]
    return width, height, codes, tuple(lut), not transparent, reduced
