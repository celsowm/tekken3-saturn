#!/usr/bin/env python3
"""Report how the private stage panorama TMD would map onto VDP1.

VDP1 draws a quad by stretching one rectangular texture over its four
corners, so a packet ports exactly only when its UVs are an axis-aligned
rectangle (in some corner order). This counts the packet kinds, the packets
whose UVs fit that shape, the distinct texture rectangles (page, CLUT, box)
and their total texel area, and the panorama's extent. It reads the
user-supplied import only and prints numbers, no art.
"""
import argparse
import json
from collections import Counter
from pathlib import Path

from build_local_paul_stage import primitives


def rectangle(uv):
    """(u0, v0, u1, v1) when the four UVs are the corners of an axis-aligned box."""
    us, vs = sorted({u for u, _ in uv}), sorted({v for _, v in uv})
    if len(uv) != 4 or len(us) != 2 or len(vs) != 2:
        return None
    if sorted(uv) != sorted((u, v) for u in us for v in vs):
        return None
    return us[0], vs[0], us[1], vs[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='private USA import directory')
    args = parser.parse_args()
    packets = primitives((args.source/'raw/056.bin').read_bytes())
    kinds, boxes, odd = Counter(), Counter(), []
    lo, hi = [1 << 30]*3, [-(1 << 30)]*3
    for rgb, material, corners in packets:
        textured = material is not None
        kinds[(len(corners), textured)] += 1
        for (x, y, z), _ in corners:
            for i, c in enumerate((x, y, z)):
                lo[i], hi[i] = min(lo[i], c), max(hi[i], c)
        if not textured:
            continue
        box = rectangle([uv for _, uv in corners])
        if box:
            boxes[(material, box)] += 1
        else:
            odd.append([uv for _, uv in corners])
    area = sum((b[2]-b[0]+1)*(b[3]-b[1]+1) for (_, b) in boxes)
    print(json.dumps(dict(
        packets=len(packets),
        kinds={f"{'quad' if n == 4 else 'tri'}-{'tex' if t else 'flat'}": c for (n, t), c in sorted(kinds.items())},
        rectangle_quads=sum(boxes.values()), other_textured=len(odd),
        other_examples=odd[:4],
        distinct_rectangles=len(boxes), rectangle_texels=area,
        rectangle_bytes_4bit=area//2,
        largest_rectangles=[[b[2]-b[0]+1, b[3]-b[1]+1] for (_, b), _ in boxes.most_common(4)],
        extent=dict(min=lo, max=hi)), indent=2))


if __name__ == '__main__':
    main()
