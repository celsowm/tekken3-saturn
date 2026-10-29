#!/usr/bin/env python3
"""Experimental rotated-quad sampler; not enabled in the Saturn renderer."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def displacement(delta, step, total, tie_up=False):
    if not total:
        return 0
    magnitude = (abs(delta) * step + (total - 1 + tie_up) // 2) // total
    return magnitude if delta >= 0 else -magnitude


def texel(step, span):
    if span < 7:
        return (2 * step + 1) * 4 // (span + 1)
    return (step * 7 + (span - 1) // 2) // span


def draw(vertices, plane=None, texture=None):
    a, b, c, d = vertices
    steps = max(abs(d[0] - a[0]), abs(d[1] - a[1]),
                abs(c[0] - b[0]), abs(c[1] - b[1]))
    result = {}
    for step in range(steps + 1):
        edges = []
        for start, end in ((a, d), (b, c)):
            dx, dy = end[0] - start[0], end[1] - start[1]
            span = max(abs(dx), abs(dy))
            edge_step = displacement(span, step, steps)
            edges.append((start[0] + displacement(dx, edge_step, span, dy < 0),
                          start[1] + displacement(dy, edge_step, span, dx < 0)))
        (x1, y1), (x2, y2) = edges
        dx, dy = x2 - x1, y2 - y1
        span = max(abs(dx), abs(dy))
        previous = None
        for j in range(span + 1):
            x = x1 + displacement(dx, j, span)
            y = y1 + displacement(dy, j, span)
            u, v = texel(j, span), texel(step, steps)
            opaque = ((plane is None or (u if plane[0] == 'u' else v) == plane[1]) and
                      (texture is None or texture[v * 8 + u] != 0))
            if previous is not None and x != previous[0] and y != previous[1] and opaque:
                px, py = previous
                extra = (x, py) if (dx >= 0) == (dy >= 0) else (px, y)
                result[extra] = (u, v)
            if opaque:
                result[(x, y)] = (u, v)
            previous = (x, y)
    return result


def main():
    points = 0
    for filename in ('vdp1_small_triangles_uv.json', 'vdp1_distorted_uv.json', 'vdp1_small_triangles_new_uv.json'):
        fixture = json.loads((ROOT / 'tests/fixtures' / filename).read_text())
        for case in fixture['cases']:
            expected = {(x, y): (u, v) for x, y, u, v in case['points']}
            actual = draw(case['vertices'])
            if actual != expected:
                missing = len(expected.keys() - actual.keys())
                extra = len(actual.keys() - expected.keys())
                wrong = sum(expected[p] != actual[p] for p in expected.keys() & actual.keys())
                raise AssertionError(f'{filename} {case["vertices"]}: missing={missing}, extra={extra}, uv={wrong}')
            points += len(expected)
    planes = []
    for filename in ('vdp1_triangle_planes.json', 'vdp1_texture_planes.json'):
        planes.extend(json.loads((ROOT / 'tests/fixtures' / filename).read_text())['cases'])
    if len(planes) != 32:
        raise AssertionError('missing transparent planes')
    for case in planes:
        actual = draw(case['vertices'], plane=(case['axis'], case['texel']))
        expected = set()
        for sample in case['samples']:
            tx, ty = sample['tile']
            for y, row in enumerate(sample['mask']):
                for x in range(8):
                    if row & (128 >> x):
                        expected.add((tx * 8 + x, ty * 8 + y))
        if actual.keys() != expected:
            raise AssertionError(f'transparent plane differs: {case["axis"]}{case["texel"]}')
    print(json.dumps({'opaque_shapes': 24, 'opaque_pixels': points,
                      'transparent_planes': len(planes)}))


if __name__ == '__main__':
    main()
