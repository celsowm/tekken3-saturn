#!/usr/bin/env python3
"""Summarize where two Saturn captures (screen.png) differ.

Prints the differing bounding box, the differing pixel count, the rows with
differences grouped into runs, and the largest channel delta. Optionally
writes an amplified difference image. Use it after a failed
compare_saturn_presentation.py run to see whether a change touched the
floor, the models or the HUD.
"""
import argparse
import json
from pathlib import Path

from PIL import Image, ImageChops


def screen(path):
    path = Path(path)
    return Image.open(path/'screen.png' if path.is_dir() else path).convert('RGB')


def runs(rows):
    out = []
    for r in rows:
        if out and out[-1][1] == r-1:
            out[-1][1] = r
        else:
            out.append([r, r])
    return out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', help='capture directory or PNG')
    parser.add_argument('candidate', help='capture directory or PNG')
    parser.add_argument('--image', help='write the difference, amplified, here')
    args = parser.parse_args()
    a, b = screen(args.baseline), screen(args.candidate)
    if a.size != b.size:
        raise SystemExit(f'size {a.size} != {b.size}')
    diff = ImageChops.difference(a, b)
    width, height = diff.size
    pixels = diff.load()
    rows, count, worst = [], 0, 0
    for y in range(height):
        line = 0
        for x in range(width):
            d = max(pixels[x, y])
            if d:
                line += 1
                worst = max(worst, d)
        if line:
            rows.append(y)
            count += line
    if args.image:
        diff.point(lambda v: min(255, v*8)).save(args.image)
    print(json.dumps(dict(size=[width, height], bbox=diff.getbbox(),
                          pixels=count, max_delta=worst,
                          row_runs=runs(rows)), indent=2))


if __name__ == '__main__':
    main()
