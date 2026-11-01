#!/usr/bin/env python3
"""Compare two same-sized images: which pixels changed, and a view of both.

Prints JSON with the changed pixel count and row range. --out writes the two
images stacked with a magenta rule, cropped by --crop and magnified with
nearest sampling by --scale, for a person to judge.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def difference(a, b):
    """Changed pixels between two image paths, with the first and last changed row."""
    first, second = (np.array(Image.open(p).convert('RGB')).astype(int) for p in (a, b))
    if first.shape != second.shape:
        raise ValueError(f'image sizes differ: {first.shape} and {second.shape}')
    changed = np.abs(first - second).sum(axis=2) > 0
    rows = np.nonzero(changed.any(axis=1))[0]
    return dict(changed_pixels=int(changed.sum()),
                first_row=int(rows[0]) if len(rows) else None,
                last_row=int(rows[-1]) if len(rows) else None)


def stacked_view(a, b, out, crop=None, scale=(2.0, 2.0)):
    views = []
    for path in (a, b):
        image = Image.open(path).convert('RGB')
        if crop:
            image = image.crop(crop)
        views.append(image.resize((round(image.width*scale[0]), round(image.height*scale[1])), Image.NEAREST))
    rule = 6
    sheet = Image.new('RGB', (views[0].width, views[0].height*2 + rule), (255, 0, 255))
    sheet.paste(views[0], (0, 0))
    sheet.paste(views[1], (0, views[0].height + rule))
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('first', type=Path)
    p.add_argument('second', type=Path)
    p.add_argument('--out', type=Path)
    p.add_argument('--crop', type=lambda s: tuple(int(v) for v in s.split(',')), help='left,top,right,bottom')
    p.add_argument('--scale', type=lambda s: tuple(float(v) for v in s.split(',')), default=(2.0, 2.0), help='x,y')
    args = p.parse_args()
    print(json.dumps(difference(args.first, args.second)))
    if args.out:
        stacked_view(args.first, args.second, args.out, args.crop, args.scale)


if __name__ == '__main__':
    main()
