#!/usr/bin/env python3
"""Compare the experimental C general rasterizer with authored captures."""
import argparse
import json
import random
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from scripts.experiment_vdp1_rotated import draw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    args = parser.parse_args()
    records, expected = [], []
    for name in ('vdp1_small_triangles_uv.json', 'vdp1_small_triangles_new_uv.json', 'vdp1_distorted_uv.json'):
        fixture = json.loads((ROOT / 'tests/fixtures' / name).read_text())
        for case in fixture['cases']:
            points = {(x, y) for x, y, _, _ in case['points']}
            left, top, right, bottom = case['bounds']
            for ty in range(top // 8, bottom // 8 + 1):
                for tx in range(left // 8, right // 8 + 1):
                    mask = [sum(0x80 >> x for x in range(8) if (tx * 8 + x, ty * 8 + y) in points)
                            for y in range(8)]
                    records.append([*[v for point in case['vertices'] for v in point], tx, ty, 1, *([0] * 64)])
                    expected.append(mask)
    for name in ('vdp1_triangle_planes.json', 'vdp1_texture_planes.json'):
        fixture = json.loads((ROOT / 'tests/fixtures' / name).read_text())
        for case in fixture['cases']:
            texture = [int((x if case['axis'] == 'u' else y) == case['texel'])
                       for y in range(8) for x in range(8)]
            for sample in case['samples']:
                records.append([*[v for point in case['vertices'] for v in point], *sample['tile'], 0, *texture])
                expected.append(sample['mask'])
    # Independent integer formula versus the C incremental stepping, including
    # both axes, direction reversals, narrow shapes and arbitrary transparency.
    rng = random.Random(0x5313)
    shapes = [[[40, 40]] * 4, [[40, 40], [48, 40], [48, 40], [40, 40]],
              [[40, 40], [40, 40], [40, 48], [40, 48]]]
    shapes.extend([[[rng.randrange(24, 73), rng.randrange(24, 73)] for _ in range(4)]
                   for _ in range(2000)])
    for i, vertices in enumerate(shapes):
        texture = [rng.randrange(2) for _ in range(64)]
        tx, ty = (5, 5) if i < 3 else (rng.randrange(2, 10), rng.randrange(2, 10))
        pixels = draw(vertices, texture=texture)
        mask = [sum(0x80 >> x for x in range(8) if (tx * 8 + x, ty * 8 + y) in pixels)
                for y in range(8)]
        records.append([*[v for point in vertices for v in point], tx, ty, 0, *texture])
        expected.append(mask)
    process = subprocess.run([str(args.runner.resolve()), '--general'],
        input=''.join(' '.join(map(str, record)) + '\n' for record in records),
        text=True, capture_output=True, check=True)
    actual = [json.loads(line) for line in process.stdout.splitlines()]
    if actual != expected:
        for i, (a, b) in enumerate(zip(actual, expected)):
            if a != b:
                raise AssertionError(f'case {i}: {a} != {b}')
        raise AssertionError('different number of masks')
    print(f'{len(records)} captured tile masks match')


if __name__ == '__main__':
    main()
