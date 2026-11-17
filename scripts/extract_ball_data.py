#!/usr/bin/env python3
"""Extract Tekken Ball's data (the ball mesh, its colours, the three ball types, the hit points) from the
volley overlay into a private blob and a C source.

The tables are volley.ovl's (Japan Rev.1 layout, as Tekken3Remake's tools/research/ball_sim.py reads them).
The blob is big endian, for direct use on the SH-2:

    "T3BL", u16 version, then
    3 ball types x 12 u16  : the option rows (0x800B6460: spike unit at byte 0, speed unit 4, gravity 8,
                             specular power byte 0xC, back colour bytes 0xD-0xF, gain 0x10, neutral zone 0x14)
    4 hit points x 3 s16   : the ball's hit volume relative to its centre (0x800B6808, stride 8)
    42 vertices x 3 s16, 42 normals x 3 s16   (0x800B64A8, 0x800B65F8)
    16 triangles x 3 u8, 32 quads x 4 u8      (0x800B6748, 0x800B6788; the fourth byte of a triangle is padding)
    3 ball types x 16 u32  : 8 triangle colours then 8 quad colours (0x800B6828, 64 bytes per type)

Output is derived from the game and stays out of git (assets/imported/).

    scripts/extract_ball_data.py --overlay third_party/Tekken3Remake/work/jp_rev1/bns/008_volley.ovl \\
        --out assets/imported/ball-probe/ball-data
"""
import argparse
import struct
from pathlib import Path

BASE = 0x800B0A10          # the mode overlays' load address (Japan Rev.1)
OPTIONS, HIT_POINTS = 0x800B6460, 0x800B6808
VERTICES, NORMALS = 0x800B64A8, 0x800B65F8
TRIANGLES, QUADS = 0x800B6748, 0x800B6788
COLOURS = 0x800B6828
TYPES, VERTEX_COUNT, TRIANGLE_COUNT, QUAD_COUNT = 3, 42, 16, 32
VERSION = 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--overlay', type=Path, required=True)
    ap.add_argument('--out', type=Path, required=True, help='prefix: writes <out>.bin and <out>.c')
    args = ap.parse_args()
    data = args.overlay.read_bytes()

    def at(addr, size):
        return data[addr - BASE:addr - BASE + size]

    blob = bytearray(b'T3BL' + struct.pack('>H', VERSION))
    for t in range(TYPES):
        blob += struct.pack('>12H', *struct.unpack_from('<12H', at(OPTIONS + 24 * t, 24)))
    for j in range(4):
        blob += struct.pack('>3h', *struct.unpack_from('<3h', at(HIT_POINTS + 8 * j, 6)))
    for table in (VERTICES, NORMALS):
        for v in range(VERTEX_COUNT):
            blob += struct.pack('>3h', *struct.unpack_from('<3h', at(table + 8 * v, 6)))
    for i in range(TRIANGLE_COUNT):
        blob += at(TRIANGLES + 4 * i, 3)
    for i in range(QUAD_COUNT):
        blob += at(QUADS + 4 * i, 4)
    for t in range(TYPES):
        blob += struct.pack('>16I', *struct.unpack_from('<16I', at(COLOURS + 64 * t, 64)))
    # sanity: every face index addresses a vertex
    body = bytes(blob)
    tri_at = 6 + TYPES * 24 + 4 * 6 + 2 * VERTEX_COUNT * 6
    tri = body[tri_at:tri_at + TRIANGLE_COUNT * 3]
    quad = body[tri_at + TRIANGLE_COUNT * 3:tri_at + TRIANGLE_COUNT * 3 + QUAD_COUNT * 4]
    if max(tri) >= VERTEX_COUNT or max(quad) >= VERTEX_COUNT:
        raise SystemExit('a face addresses a vertex past the mesh: the overlay is not Japan Rev.1 volley.ovl')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.with_suffix('.bin').write_bytes(blob)
    lines = ['/* Derived from the game by scripts/extract_ball_data.py; private, not in git. */',
             '#include <stdint.h>', f'const uint32_t t3_ball_data_blob_size={len(blob)}u;',
             'const uint8_t t3_ball_data_blob[]={']
    for i in range(0, len(blob), 16):
        lines.append(','.join(str(b) for b in blob[i:i + 16]) + ',')
    lines.append('};')
    args.out.with_suffix('.c').write_text('\n'.join(lines) + '\n')
    print(f'{VERTEX_COUNT} vertices, {TRIANGLE_COUNT} triangles, {QUAD_COUNT} quads, {TYPES} ball types, '
          f'{len(blob)} bytes -> {args.out}.bin/.c')


if __name__ == '__main__':
    main()
