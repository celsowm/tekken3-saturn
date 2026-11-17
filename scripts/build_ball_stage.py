#!/usr/bin/env python3
"""Build STG19.BIN, Tekken Ball's court, from the private USA import (the converter's stage 19 pictures).

Only user-supplied files are read. No retail data is shipped with this tool. Stage 19 (stg_v, archive 55) is not a
TMD stage: its backdrop is a flat 1024 x 256 tile-map picture (32 x 8 tiles of 32 x 32) that the game scrolls with
the camera, with the sky colour (0x21, 0x3A, 0x94) above and a repeating floor (10 x 10 tiles of 64 x 64). The file
is in the layout of src/game/stage_format.h (the same as scripts/build_stage_packs.py's), so the existing loader
reads it; the panorama is the strip's bottom 240 rows (the top 16 are sky), not a cylinder, and the Saturn side
scrolls it flat (docs/TEKKEN_BALL_PLAN.md). The floor is unshaded (a standard floor, level 128).

    python3 scripts/export_ball_stage_pictures.py --image "roms/Tekken 3 USA/Tekken 3 (USA).cue" --out assets/imported/ball-probe/stage-usa
    build/asset-venv/bin/python scripts/build_ball_stage.py --pictures assets/imported/ball-probe/stage-usa/stages/v \\
        --exe assets/imported/tekken3-usa/SLUS_004.02 --out build/ball-stage
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

import build_stage_packs as bp

STAGE = 19
SKY = (0x21, 0x3A, 0x94)       # volley.ovl FUN_800B5CC4: the TILE primitive behind the strip
CUT_ROWS = 256 - bp.ROWS       # the strip's top rows, sky only


def rgb555(r, g, b):
    return 0x8000 | (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10


def to_555(rgba):
    a = np.asarray(rgba, dtype=np.uint8).reshape(-1, 4)
    return (a[:, 0] >> 3).astype(np.uint16) | (a[:, 1] >> 3).astype(np.uint16) << 5 | (a[:, 2] >> 3).astype(np.uint16) << 10, a[:, 3] > 0


def panorama(strip):
    """The strip's bottom rows as 8-bit indices (0 transparent) and a 255-colour palette."""
    rows = strip.crop((0, CUT_ROWS, bp.COLUMNS, 256))
    colour, opaque = to_555(rows)
    rgb = np.stack([(colour[opaque] >> (5 * k)) & 31 for k in range(3)], axis=1).astype(np.uint8) * 8
    distinct = sorted(set(int(c) for c in colour[opaque]))
    if len(distinct) <= 255:
        palette = distinct
        lookup = {c: i for i, c in enumerate(palette)}
        chosen = np.array([lookup[int(c)] for c in colour[opaque]])
    else:
        # One 8-bit CRAM bank, as the TMD stages: median cut with k-means, no dither (the layer is magnified)
        quantized = Image.fromarray(rgb.reshape(1, -1, 3), 'RGB').quantize(
            colors=255, method=Image.Quantize.MEDIANCUT, kmeans=8, dither=Image.Dither.NONE)
        levels = np.minimum(31, (np.array(quantized.getpalette()[:255 * 3]).reshape(-1, 3) + 4) // 8)
        palette = [int(0x8000 | r | g << 5 | b << 10) for r, g, b in levels]
        chosen = np.array(quantized).reshape(-1)
    indices = np.zeros(len(colour), dtype=np.uint8)
    indices[opaque] = chosen + 1
    got = np.array(palette)[chosen]
    error = max(int(np.abs(((got >> (5 * k)) & 31) - ((colour[opaque] >> (5 * k)) & 31)).max()) for k in range(3))
    return bytes(indices), [0] + [int(c) | 0x8000 for c in palette], dict(colours=len(distinct), max_error=error,
                                                                           opaque=round(float(opaque.mean()), 3))


def floor(pattern):
    """The floor's 10 x 10 tile picture as the pack's FLOOR_W x FLOOR_H bitmap: tile (cx, cy) shows entry
    ((cy + 5) % 10, (cx + 5) % 10), as build_stage_packs.floor_bitmap does."""
    columns, rows = bp.FLOOR_W // 64, bp.FLOOR_H // 64
    colour, opaque = to_555(pattern)
    colour = colour.reshape(640, 640)
    opaque = opaque.reshape(640, 640)
    wide = [[colour[r * 64:(r + 1) * 64, c * 64:(c + 1) * 64] for c in range(10)] for r in range(10)]
    colours = sorted({int(c) for r in wide for tile in r for c in tile.reshape(-1)} - {0})
    if len(colours) > 255:
        raise SystemExit('the floor exceeds 255 colours')
    lookup = np.zeros(1 << 16, dtype=np.uint8)
    for i, c in enumerate(colours):
        lookup[c] = i + 1
    indices = np.zeros((bp.FLOOR_H, bp.FLOOR_W), dtype=np.uint8)
    for cy in range(rows):
        for cx in range(columns):
            tile = wide[(cy + 5) % 10][(cx + 5) % 10]
            indices[cy * 64:(cy + 1) * 64, cx * 64:(cx + 1) * 64] = lookup[tile]
    cut = any(not np.array_equal(wide[r][c], wide[r][(c + columns) % 10]) or
              not np.array_equal(wide[r][c], wide[(r + rows) % 10][c]) for r in range(10) for c in range(10))
    return bytes(indices.reshape(-1)), [0] + [bp.scale_colour(c, 128) for c in colours], cut


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--pictures', type=Path, required=True, help='the converter\'s stages/v folder')
    ap.add_argument('--exe', type=Path, required=True, help='the USA SLUS_004.02')
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    strip = Image.open(args.pictures / 'tiles.png').convert('RGBA')
    pattern = Image.open(args.pictures / 'floor.png').convert('RGBA')
    if strip.size != (bp.COLUMNS, 256) or pattern.size != (640, 640):
        raise SystemExit('unexpected picture sizes: not the converter\'s stage 19')
    exe = args.exe.read_bytes()
    shadow = exe[bp.usa_file_offset(bp.STAGE_RECORDS + bp.STAGE_RECORD_BYTES * STAGE) + 0x10:][:3]
    picture, picture_palette, quantization = panorama(strip)
    floor_bytes, floor_palette, cut = floor(pattern)
    payload = bp.pack(STAGE, 0, rgb555(*SKY), rgb555(*shadow), 128, floor_palette, floor_bytes, picture_palette, picture)
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / f'STG{STAGE}.BIN').write_bytes(payload)
    (args.out / f'STG{STAGE}.json').write_text(json.dumps(dict(
        stage=STAGE, shadow=list(shadow), sky=list(SKY), panorama=dict(rows=bp.ROWS, cut_rows=CUT_ROWS, **quantization),
        floor_colours=len(floor_palette) - 1, floor_pattern_cut=cut, output_bytes=len(payload),
        output_sha256=hashlib.sha256(payload).hexdigest()), indent=2) + '\n')
    print(f'STG{STAGE}.BIN: {len(payload)} bytes, panorama {quantization}, floor {len(floor_palette) - 1} colours -> {args.out}')


if __name__ == '__main__':
    main()
