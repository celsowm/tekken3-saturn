#!/usr/bin/env python3
"""Compare the bright spark pixels of an aligned PS1 and Saturn frame.

The PS1 frame (368x480) is cropped to rows 20..467 and resized to 320x224, the
mapping the Saturn HUD uses. Spark pixels are the bright yellow/orange ones
(R>200, G>140, B<150) outside the fighters' red clothing; an optional --exclude
rectangle removes an effect the Saturn does not draw (the power glow). The
comparison is descriptive: counts, centroids, bounding boxes and the overlap of
the 8x8 cells that contain a spark pixel. It is not a pixel-parity claim."""
import argparse, json
import numpy as np
from PIL import Image


def small_blobs(mask, max_pixels=30, max_extent=8):
    """Keep only small connected blobs (sparks); hair, clothing and HUD are larger."""
    h, w = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    keep = np.zeros_like(mask, dtype=bool)
    for y0, x0 in zip(*np.nonzero(mask)):
        if seen[y0, x0]:
            continue
        stack, blob = [(y0, x0)], []
        seen[y0, x0] = True
        while stack:
            y, x = stack.pop()
            blob.append((y, x))
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    ny, nx = y + dy, x + dx
                    if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                        seen[ny, nx] = True
                        stack.append((ny, nx))
        ys = [p[0] for p in blob]
        xs = [p[1] for p in blob]
        if len(blob) <= max_pixels and max(ys) - min(ys) < max_extent and max(xs) - min(xs) < max_extent:
            for y, x in blob:
                keep[y, x] = True
    return keep


def sparks(img, exclude):
    a = np.asarray(img.convert('RGB')).astype(int)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    mask = (r > 200) & (g > 140) & (b < 150)
    mask[:45, :] = False  # HUD rows
    mask = small_blobs(mask)
    if exclude:
        left, top, right, bottom = exclude
        mask[top:bottom, left:right] = False
    return mask


def describe(mask):
    ys, xs = np.nonzero(mask)
    if not len(xs):
        return {'pixels': 0}
    cells = set(zip((ys // 8).tolist(), (xs // 8).tolist()))
    return {'pixels': int(len(xs)), 'centroid': [round(float(xs.mean()), 1), round(float(ys.mean()), 1)],
            'bbox': [int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max())], 'cells': sorted(cells)}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('ps1')
    p.add_argument('saturn')
    p.add_argument('--exclude', type=int, nargs=4, metavar=('L', 'T', 'R', 'B'))
    p.add_argument('--search-shift', action='store_true',
                   help='also report the best global translation of the Saturn spark mask onto the PS1 one')
    p.add_argument('--out')
    a = p.parse_args()
    ps1 = Image.open(a.ps1).convert('RGB')
    if ps1.size == (368, 480):
        ps1 = ps1.crop((0, 20, 368, 468)).resize((320, 224), Image.NEAREST)  # point sampling: smoothing lowers the peaks
    sat = Image.open(a.saturn).convert('RGB')
    m1, m2 = sparks(ps1, a.exclude), sparks(sat, a.exclude)
    d1, d2 = describe(m1), describe(m2)
    c1, c2 = set(map(tuple, d1.get('cells', []))), set(map(tuple, d2.get('cells', [])))
    union = len(c1 | c2)
    report = {'ps1': {k: v for k, v in d1.items() if k != 'cells'}, 'saturn': {k: v for k, v in d2.items() if k != 'cells'},
              'ps1_cells': len(c1), 'saturn_cells': len(c2), 'shared_cells': len(c1 & c2),
              'cell_jaccard': round(len(c1 & c2) / union, 3) if union else None,
              'excluded': a.exclude, 'scope': 'descriptive spark overlap, not pixel parity'}
    # Colour of the paired spark pixels. The same operator (brightest pixel of the 3 x 3
    # around the location) is applied to both frames: applying it to one side only made
    # the PS1's sparks look whiter than the Saturn's.
    def nbmax(img, y, x):
        reg = img[max(0, y - 1):y + 2, max(0, x - 1):x + 2].reshape(-1, 3)
        return reg[reg.sum(1).argmax()]

    def dil1(m):
        o = m.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                o |= np.roll(np.roll(m, dy, 0), dx, 1)
        return o
    pa_img, sa_img = np.asarray(ps1).astype(int), np.asarray(sat).astype(int)
    ys, xs = np.nonzero(m2 & dil1(m1))
    if len(ys):
        pa = np.array([nbmax(pa_img, y, x) for y, x in zip(ys, xs)])
        sb = np.array([nbmax(sa_img, y, x) for y, x in zip(ys, xs)])
        report['paired_colour'] = {'pairs': int(len(ys)), 'ps1_mean_rgb': pa.mean(0).round(1).tolist(),
                                   'saturn_mean_rgb': sb.mean(0).round(1).tolist(),
                                   'mean_abs_difference_rgb': np.abs(pa - sb).mean(0).round(1).tolist()}
    if a.search_shift:
        def dilate(m):
            o = m.copy()
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    o |= np.roll(np.roll(m, dy, 0), dx, 1)
            return o
        d1m, scores = dilate(m1), []
        for dy in range(-10, 11):
            for dx in range(-14, 15):
                s2 = np.roll(np.roll(m2, dy, 0), dx, 1)
                scores.append((int((d1m & s2).sum() + (m1 & dilate(s2)).sum()), dx, dy))
        scores.sort(reverse=True)
        zero = [sc for sc in scores if sc[1] == 0 and sc[2] == 0][0][0]
        report['shift_search'] = {'best': scores[0], 'zero_shift_score': zero,
                                  'meaning': 'score = spark pixels within one pixel of the other mask, both ways; best is (score, dx, dy) applied to the Saturn mask'}
    print(json.dumps(report, indent=2))
    if a.out:
        open(a.out, 'w').write(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
