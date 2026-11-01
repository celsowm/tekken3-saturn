#!/usr/bin/env python3
"""Draw a fighter's projected faces from a probe dump, kept and culled, to see what the Saturn draws.

Takes --dump-range dumps of Low and High Work RAM (range-0.bin, range-1.bin) and the actor_t offsets of that build.
Faces of --row come out in KMD order. Writes one enlarged PNG and lists the row's vertices and faces.
"""
import argparse
import struct
from pathlib import Path

from PIL import Image, ImageDraw

import kmd_to_ska

LOW, HIGH = 0x00200000, 0x06000000
SCALE = 8


def memory(low, high):
    def read(address, count):
        address &= 0x07FFFFFF
        if LOW <= address < LOW + len(low):
            return low[address - LOW:address - LOW + count]
        if HIGH <= address < HIGH + len(high):
            return high[address - HIGH:address - HIGH + count]
        raise ValueError(f'{address:#x} is outside the dumps')
    return read


def corner_area(a, b, c, d):
    """The SDK's screen cull: a face with this at zero or below is not drawn."""
    return (c[0] - a[0]) * (d[1] - b[1]) - (d[0] - b[0]) * (c[1] - a[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dump', type=Path, help='directory holding range-0.bin and range-1.bin')
    parser.add_argument('kmd', type=Path)
    parser.add_argument('--actors', type=lambda s: int(s, 0), required=True, help='address of g_actors')
    parser.add_argument('--actor', type=int, default=0)
    parser.add_argument('--actor-bytes', type=int, required=True)
    parser.add_argument('--projected', type=int, required=True, help='offsetof actor_t.projected')
    parser.add_argument('--mesh-indices', type=int, required=True)
    parser.add_argument('--vertex-count', type=int, required=True)
    parser.add_argument('--face-count', type=int, required=True)
    parser.add_argument('--row', type=int, nargs='+', default=[19])
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--list', action='store_true', help='print each face')
    args = parser.parse_args()
    read = memory((args.dump / 'range-0.bin').read_bytes(), (args.dump / 'range-1.bin').read_bytes())
    actor = args.actors + args.actor * args.actor_bytes
    raw_at = struct.unpack('>I', read(actor + args.projected, 4))[0]
    indices_at = struct.unpack('>I', read(actor + args.mesh_indices, 4))[0]
    vertices = struct.unpack('>H', read(actor + args.vertex_count, 2))[0]
    faces = struct.unpack('>H', read(actor + args.face_count, 2))[0]
    print(f'raw {raw_at:#x} indices {indices_at:#x} vertices {vertices} faces {faces}')
    # sat_projected_vertex_t: x, y and a 16.16 depth.
    points = [struct.unpack('>hhi', read(raw_at + 8 * i, 8)) for i in range(vertices)]
    indices = struct.unpack(f'>{faces * 4}H', read(indices_at, faces * 8))
    model = kmd_to_ska.parse_kmd(args.kmd.read_bytes())
    first = 0
    spans = {}
    for row in model.rows[:kmd_to_ska.PART_ROWS[18]]:  # later rows are attachments the mesh leaves out
        if row.vert is not None:
            spans[row.row] = (first, first + len(row.faces))
            first += len(row.faces)
    print('mesh faces in KMD order', first, 'of', faces)
    chosen = [f for r in args.row if r in spans for f in range(*spans[r])]
    used = [points[indices[4 * f + k]] for f in chosen for k in range(4)]
    left = min(p[0] for p in used) - 4
    top = min(p[1] for p in used) - 4
    width = max(p[0] for p in used) + 4 - left
    height = max(p[1] for p in used) + 4 - top
    print(f'box x {left}..{left + width} y {top}..{top + height} z {min(p[2] for p in used)}..{max(p[2] for p in used)}')
    image = Image.new('RGB', (width * SCALE, height * SCALE), (24, 24, 32))
    draw = ImageDraw.Draw(image)
    place = lambda p: ((p[0] - left) * SCALE, (p[1] - top) * SCALE)
    kept = culled = 0
    for f in chosen:
        i = indices[4 * f:4 * f + 4]
        area = corner_area(*(points[v] for v in i))
        if area > 0:
            kept += 1
            outline = (90, 220, 90)
            fill = (30, 90, 30)
        else:
            culled += 1
            outline = (220, 70, 70)
            fill = None
        if args.list:
            print(f, [points[v] for v in i], 'area', area)
        draw.polygon([place(points[i[k]]) for k in (0, 1, 2, 3)], outline=outline, fill=fill)
    print(f'kept {kept} culled {culled}')
    image.save(args.out)


if __name__ == '__main__':
    main()
