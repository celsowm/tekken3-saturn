#!/usr/bin/env python3
"""Compare full health-bar fill bounds in captured PS1 and Saturn images."""
import argparse
import json
from pathlib import Path
from PIL import Image


def spans(image, row):
    pixels = [x for x in range(image.width)
              if (lambda c: c[1] > 120 and c[2] > 110 and c[0] < 140
                  and min(c[1], c[2]) - c[0] >= 30)(image.getpixel((x, row)))]
    result = []
    for x in pixels:
        if not result or x != result[-1][1] + 1:
            result.append([x, x])
        else:
            result[-1][1] = x
    return [pair for pair in result if pair[1] - pair[0] >= 20]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ps1', type=Path, help='368x480 original screenshot')
    parser.add_argument('saturn', type=Path, help='320x224 screenshot with both bars full')
    args = parser.parse_args()
    ps1 = Image.open(args.ps1).convert('RGB')
    saturn = Image.open(args.saturn).convert('RGB')
    if ps1.size != (368, 480) or saturn.size != (320, 224):
        raise ValueError('unexpected screenshot dimensions')
    original = ps1.crop((0, 20, 368, 468)).resize((320, 224), Image.Resampling.NEAREST)
    a, b = spans(original, 14), spans(saturn, 14)
    if len(a) != 2 or len(b) != 2:
        raise ValueError('expected two full cyan bar fills in each capture')
    differences = [[b[p][end] - a[p][end] for end in range(2)] for p in range(2)]
    if any(abs(d) > 2 for pair in differences for d in pair):
        raise ValueError(f'bar bounds differ: {differences}')
    print(json.dumps({'ps1': a, 'saturn': b, 'difference_pixels': differences}, indent=2))


if __name__ == '__main__':
    main()
