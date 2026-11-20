#!/usr/bin/env python3
"""Host study for sharper stage backdrops (docs/STAGE_BACKDROP_SHARPNESS_PLAN.md, step 0).

`bake` casts each stage's panorama at the candidate cylinder sizes and keeps the
raw RGB555 ray samples under --cache. `report` reads them back and prints, per
stage and size, what each storage format costs:

  * 255 global colours (today's picture; also format B's palette): RMS error in
    5-bit channel steps, and the unique 8 x 8 cells (plain and with mirroring);
  * 16 palettes of 15 colours + transparent per 8 x 8 cell (format A): the same
    error after clustering the cells into palettes;
  * bytes for each, and for the filters compared (2 x 2 mean and a single ray).

Prints numbers only.
"""
import argparse
import json
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np

from build_stage_packs import STAGE_NUMBERS, cylinder_colour, read_arc, stage_metadata, box_filter

# (columns, rows, rows per unit of tangent): today, wider, wider and taller.
SIZES = ((1024, 240, 200), (2048, 240, 200), (2048, 352, 293))
CELL = 8
PALETTES, PALETTE_COLOURS = 16, 15


def cache_path(cache, stage, size, supersample):
    return cache/f'stage{stage:02}_{size[0]}x{size[1]}_ss{supersample}.npy'


def bake(job):
    source, cache, stage, size, supersample = job
    path = cache_path(cache, stage, size, supersample)
    if path.exists():
        return str(path)
    tmd = (source/f'raw/{56 + stage:03}.bin').read_bytes()
    height, _ = stage_metadata((source/'SLUS_004.02').read_bytes(), stage)
    vram, _, _ = read_arc((source/f'raw/{36 + stage:03}.bin').read_bytes())
    colour, _ = cylinder_colour(tmd, vram, height, supersample, size[0], size[1], size[2])
    np.save(path, colour.astype(np.uint16))
    return str(path)


def channels(colour):
    return np.stack([(colour >> (5*k)) & 31 for k in range(3)], axis=-1).astype(np.int32)


def quantize(pixels, count):
    """Palette (count x 3, 5-bit) and index per pixel by median cut with k-means."""
    from PIL import Image
    if len(pixels) == 0:
        return np.zeros((count, 3), np.int32), np.zeros(0, np.int64)
    strip = Image.fromarray((pixels*8).astype(np.uint8).reshape(1, -1, 3), 'RGB')
    q = strip.quantize(colors=count, method=Image.Quantize.MEDIANCUT, kmeans=4, dither=Image.Dither.NONE)
    levels = np.array(q.getpalette()[:count*3]).reshape(-1, 3)
    levels = np.minimum(31, (levels + 4)//8)
    levels = np.concatenate([levels, np.zeros((count - len(levels), 3), np.int64)])
    return levels.astype(np.int32), np.array(q).reshape(-1).astype(np.int64)


def nearest(pixels, palette):
    """Index of the closest palette entry for each pixel, and the squared error."""
    d = ((pixels[:, None, :] - palette[None, :, :])**2).sum(-1)
    at = d.argmin(1)
    return at, d[np.arange(len(pixels)), at]


def global_255(rgb, opaque):
    pixels = rgb[opaque]
    # Fewer distinct colours than entries: exact.
    packed = np.unique(pixels[:, 0] | pixels[:, 1] << 5 | pixels[:, 2] << 10)
    if len(packed) <= 255:
        palette = np.stack([packed & 31, packed >> 5 & 31, packed >> 10 & 31], axis=1).astype(np.int32)
    else:
        palette, _ = quantize(pixels, 255)
    at, err = nearest(pixels, palette)
    index = np.zeros(opaque.shape, np.int64)
    index[opaque] = at + 1
    return index, float(err.sum()), palette


def cells_of(array, rows, columns):
    return array.reshape(rows//CELL, CELL, columns//CELL, CELL, *array.shape[2:]).swapaxes(1, 2)


def unique_cells(index, rows, columns):
    cells = cells_of(index, rows, columns).reshape(-1, CELL*CELL)
    plain = len({c.tobytes() for c in cells.astype(np.uint8)})
    seen = set()
    for c in cells.astype(np.uint8).reshape(-1, CELL, CELL):
        seen.add(min(c.tobytes(), c[:, ::-1].tobytes(), c[::-1].tobytes(), c[::-1, ::-1].tobytes()))
    return int(len(cells)), plain, len(seen)


def format_a(rgb, opaque, rows, columns):
    """16 palettes of 15 colours for the 8 x 8 cells; returns the squared error, the palettes used and
    the picture as those palettes show it (5-bit channels, rows x columns x 3)."""
    pix = cells_of(rgb, rows, columns).reshape(-1, CELL*CELL, 3)
    op = cells_of(opaque, rows, columns).reshape(-1, CELL*CELL)
    count = len(pix)
    mean = np.array([p[o].mean(0) if o.any() else (0, 0, 0) for p, o in zip(pix, op)])
    has = op.any(1)
    # Start: split the cells by mean colour (median cut over cells).
    groups = np.zeros(count, np.int64)
    for _ in range(4):
        new = groups.copy()
        for g in range(int(groups.max()) + 1):
            members = np.nonzero((groups == g) & has)[0]
            if len(members) < 2:
                continue
            axis = mean[members].var(0).argmax()
            cut = np.median(mean[members][:, axis])
            high = members[mean[members][:, axis] > cut]
            new[high] = int(groups.max()) + 1 + g
        _, groups = np.unique(new, return_inverse=True)
    groups = np.minimum(groups, PALETTES - 1)
    total = 0.0
    for _ in range(4):
        palettes = []
        for g in range(PALETTES):
            members = np.nonzero((groups == g) & has)[0]
            p = pix[members][op[members]]
            palettes.append(quantize(p, PALETTE_COLOURS)[0] if len(p) else np.zeros((PALETTE_COLOURS, 3), np.int32))
        # Reassign each cell to the palette with the least error.
        err = np.zeros((count, PALETTES))
        for g, pal in enumerate(palettes):
            d = ((pix[:, :, None, :] - pal[None, None, :, :])**2).sum(-1).min(-1)
            err[:, g] = (d*op).sum(1)
        groups = err.argmin(1)
        total = float(err[np.arange(count), groups][has].sum())
    shown = np.zeros_like(pix)
    for g, pal in enumerate(palettes):
        members = np.nonzero(groups == g)[0]
        if len(members):
            d = ((pix[members][:, :, None, :] - pal[None, None, :, :])**2).sum(-1)
            shown[members] = pal[d.argmin(-1)]
    picture = shown.reshape(rows//CELL, columns//CELL, CELL, CELL, 3).swapaxes(1, 2).reshape(rows, columns, 3)
    return total, int(len(np.unique(groups[has]))), np.where(opaque[..., None], picture, 0)


def report_one(job):
    cache, stage, size, supersample = job
    columns, rows, _ = size
    raw = np.load(cache_path(cache, stage, size, supersample)).astype(np.int64)
    out = []
    for name, colour in (('mean', box_filter(raw, columns, rows, supersample).reshape(rows, columns)),
                         ('centre', np.where(raw.reshape(rows, supersample, columns, supersample)
                                             [:, supersample//2, :, supersample//2] != 0,
                                             raw.reshape(rows, supersample, columns, supersample)
                                             [:, supersample//2, :, supersample//2], 0))):
        opaque = colour != 0
        rgb = channels(colour)
        n = int(opaque.sum())
        index, err255, _ = global_255(rgb, opaque)
        cells, plain, flipped = unique_cells(index, rows, columns)
        err16, used, _ = format_a(rgb, opaque, rows, columns)
        out.append(dict(stage=stage, size=f'{columns}x{rows}', filter=name, opaque=round(n/opaque.size, 3),
                        rms_255=round((err255/max(n, 1)/3)**.5, 3), rms_16x15=round((err16/max(n, 1)/3)**.5, 3),
                        palettes_used=used, cells=cells, unique_cells=plain, unique_cells_mirrored=flipped,
                        bytes_a=cells*32 + cells*2, bytes_b_plain=plain*64 + cells*2,
                        bytes_b_mirrored=flipped*64 + cells*2))
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    for name in ('bake', 'report'):
        s = sub.add_parser(name)
        s.add_argument('--cache', type=Path, required=True)
        s.add_argument('--stages', default=','.join(map(str, STAGE_NUMBERS)))
        s.add_argument('--supersample', type=int, default=2)
        s.add_argument('--jobs', type=int, default=8)
        if name == 'bake':
            s.add_argument('--source', type=Path, required=True)
    a = p.parse_args()
    stages = [int(s) for s in a.stages.split(',')]
    a.cache.mkdir(parents=True, exist_ok=True)
    with ProcessPoolExecutor(a.jobs) as pool:
        if a.command == 'bake':
            for path in pool.map(bake, [(a.source.resolve(), a.cache.resolve(), s, size, a.supersample)
                                        for s in stages for size in SIZES]):
                print(path, flush=True)
        else:
            for rows in pool.map(report_one, [(a.cache.resolve(), s, size, a.supersample)
                                              for s in stages for size in SIZES]):
                for row in rows:
                    print(json.dumps(row), flush=True)


if __name__ == '__main__':
    main()
