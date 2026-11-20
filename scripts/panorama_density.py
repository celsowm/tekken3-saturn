#!/usr/bin/env python3
"""Report how many source texels each stage panorama packs into one cylinder texel.

For every textured panorama triangle (placed as panorama_raycast.py places it),
this compares its area in the TMD's texture (texels) with its area on the baked
cylinder as seen from the bake point (cylinder texels). The square root of the
ratio is the linear source density: above 1 the cylinder drops source detail,
below 1 it already holds every source texel. Figures are weighted by cylinder
area, so they describe what the picture shows. Prints numbers only, no art.
"""
import argparse
import json
from pathlib import Path

import numpy as np

from build_stage_packs import (BAKE_HEIGHT, PANO_COLUMNS as COLUMNS, PANO_ROWS as ROWS,
                               PANO_ROWS_PER_TAN as ROWS_PER_TAN, STAGE_NUMBERS, TAN_TOP, read_arc, stage_metadata)
from panorama_raycast import FLAT, Panorama


def cylinder_coords(points):
    """Column and row (cylinder texels) of PS1-axis points seen from the bake point."""
    x, y, z = points[..., 0], points[..., 1] + BAKE_HEIGHT, points[..., 2]
    column = np.arctan2(x, z) % (2*np.pi) * COLUMNS / (2*np.pi)
    row = (TAN_TOP + y/np.hypot(x, z)) * ROWS_PER_TAN
    return column, row


def area(u, v):
    return 0.5*np.abs((u[:, 1]-u[:, 0])*(v[:, 2]-v[:, 0]) - (u[:, 2]-u[:, 0])*(v[:, 1]-v[:, 0]))


def density(source, stage):
    tmd = (source/f'raw/{56 + stage:03}.bin').read_bytes()
    height, _ = stage_metadata((source/'SLUS_004.02').read_bytes(), stage)
    vram, _, _ = read_arc((source/f'raw/{36 + stage:03}.bin').read_bytes())
    scene = Panorama(tmd, vram, height)
    textured = scene.pages != FLAT
    with np.errstate(all='ignore'):
        column, row = cylinder_coords(scene.tris[textured])
    # Unwrap each triangle's columns around its first corner.
    column = column[:, :1] + (column - column[:, :1] + COLUMNS/2) % COLUMNS - COLUMNS/2
    on = area(column, row)
    uv = scene.uvs[textured]
    texels = area(uv[..., 0], uv[..., 1])
    rows = np.clip(row, 0, ROWS)
    with np.errstate(all='ignore'):
        visible = np.isfinite(on) & (on > 0) & (area(column, rows) > 0.25) & (texels > 0)
    linear = np.sqrt(texels[visible]/on[visible])
    weight = area(column, rows)[visible]
    order = np.argsort(linear)
    cumulative = np.cumsum(weight[order])/weight.sum()
    pick = {p: round(float(linear[order][np.searchsorted(cumulative, p/100)]), 2) for p in (10, 25, 50, 75, 90)}
    return dict(stage=stage, triangles=int(visible.sum()), source_per_cylinder_texel=pick,
                share_above_1=round(float(weight[linear > 1].sum()/weight.sum()), 3),
                share_above_1_8=round(float(weight[linear > 1.8].sum()/weight.sum()), 3))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True, help='private import with raw/ and SLUS_004.02')
    p.add_argument('--stages', default=','.join(map(str, STAGE_NUMBERS)))
    a = p.parse_args()
    for stage in (int(s) for s in a.stages.split(',')):
        print(json.dumps(density(a.source.resolve(), stage)))


if __name__ == '__main__':
    main()
