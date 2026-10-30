#!/usr/bin/env python3
"""Compare the hit burst of an aligned PS1 and Saturn frame inside a rectangle.

The PS1 frame (368x480) is cropped to rows 20..467 and resized to 320x224, the
mapping the Saturn HUD uses. Inside the given Saturn rectangle (--rect L T R B)
it reports each side's mean colour and fire-pixel count (R>140, B<110), the
mean absolute colour difference, and the overlap of the two fire masks. Writes a
side-by-side image (PS1, Saturn, absolute difference) with --out. Descriptive,
not a pixel-parity claim."""
import argparse, json
import numpy as np
from PIL import Image


def ps1_to_saturn(path):
    img = Image.open(path).convert('RGB').crop((0, 20, 368, 468))
    return img.resize((320, 224), Image.BILINEAR)


def fire(a):
    return (a[..., 0] > 140) & (a[..., 2] < 110)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('ps1')
    p.add_argument('saturn')
    p.add_argument('--rect', type=int, nargs=4, metavar=('L', 'T', 'R', 'B'), required=True)
    p.add_argument('--out')
    a = p.parse_args()
    ps1 = ps1_to_saturn(a.ps1)
    sat = Image.open(a.saturn).convert('RGB').resize((320, 224))
    l, t, r, b = a.rect
    x = np.asarray(ps1).astype(int)[t:b, l:r]
    y = np.asarray(sat).astype(int)[t:b, l:r]
    fx, fy = fire(x), fire(y)
    union = int((fx | fy).sum())
    report = {
        'rect': a.rect,
        'ps1_mean': [round(float(v), 1) for v in x.reshape(-1, 3).mean(0)],
        'saturn_mean': [round(float(v), 1) for v in y.reshape(-1, 3).mean(0)],
        'mean_abs_diff': round(float(np.abs(x - y).mean()), 2),
        'ps1_fire_pixels': int(fx.sum()), 'saturn_fire_pixels': int(fy.sum()),
        'fire_overlap_jaccard': round(float((fx & fy).sum()) / union, 3) if union else None,
    }
    if a.out:
        w = Image.new('RGB', (3 * 320, 224))
        w.paste(ps1, (0, 0))
        w.paste(sat, (320, 0))
        d = np.abs(np.asarray(ps1).astype(int) - np.asarray(sat).astype(int)).clip(0, 255).astype('uint8')
        w.paste(Image.fromarray(d), (640, 0))
        w.save(a.out)
    print(json.dumps(report))


if __name__ == '__main__':
    main()
