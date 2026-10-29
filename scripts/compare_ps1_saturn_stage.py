#!/usr/bin/env python3
"""Report descriptive image errors for matched PS1/Saturn stage captures."""
import argparse
import json
from pathlib import Path
from PIL import Image

REGIONS = {'panorama': (30, 35, 290, 60), 'far_floor': (30, 175, 290, 190),
           'near_floor': (30, 190, 290, 218)}


def compare(original, saturn, rect):
    left, top, right, bottom = rect
    sums = [0, 0, 0]
    count = 0
    for y in range(top, bottom):
        for x in range(left, right):
            a, b = original.getpixel((x, y)), saturn.getpixel((x, y))
            for c in range(3):
                sums[c] += abs(a[c] - b[c])
            count += 1
    return {'pixels': count, 'absolute_error_rgb': [round(s / count, 3) for s in sums],
            'mean_absolute_error': round(sum(sums) / (3 * count), 3)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('ps1', type=Path)
    parser.add_argument('saturn', type=Path, nargs='+')
    args = parser.parse_args()
    original = Image.open(args.ps1).convert('RGB')
    if original.size != (368, 480):
        raise ValueError('expected original 368x480 capture')
    original = original.crop((0, 20, 368, 468)).resize((320, 224), Image.Resampling.NEAREST)
    result = {}
    for path in args.saturn:
        image = Image.open(path).convert('RGB')
        if image.size != (320, 224):
            raise ValueError(f'unexpected size: {path}')
        result[str(path)] = {name: compare(original, image, rect) for name, rect in REGIONS.items()}
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
