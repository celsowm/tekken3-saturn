#!/usr/bin/env python3
"""Report what each fighter's textures cost in VDP1 VRAM for a texture bake.

The 8x8 bake (the shipped one, texels_8x8) is compared with native LUT4 bakes at several size
caps (psx_character_textures.bake_face_lut4): unique textures, pixel bytes, 16-colour
tables, faces whose 8-bit colours had to be reduced. Reads the private USA import.

    scripts/fighter_texture_budget.py --raw assets/imported/tekken3-usa-full/raw --caps 16 24 32 [--only 11 9]
"""
import argparse
import json
from pathlib import Path

from fighter_roster import ROSTER, records
from kmd_to_ska import PART_ROWS, SECOND_MESH, parse_kmd
from psx_character_textures import bake_face_lut4, corners_of, load_character_vram


def texels_reached(uv, width, height):
    """Distinct source texels a width x height patch samples, and the texels of the face's UV box."""
    corners = corners_of(uv)
    seen = set()
    for y in range(height):
        fy = y / (height-1) if height > 1 else 0
        for x in range(width):
            fx = x / (width-1) if width > 1 else 0
            weights = ((1-fx)*(1-fy), fx*(1-fy), fx*fy, (1-fx)*fy)
            seen.add(tuple(round(sum(p[axis]*w for p, w in zip(corners, weights))) for axis in range(2)))
    us, vs = [p[0] for p in uv], [p[1] for p in uv]
    return len(seen), (max(us)-min(us)+1) * (max(vs)-min(vs)+1)


def faces_of(model):
    for part, row_index in enumerate(PART_ROWS[:18]):
        for ri in [row_index] + ([row_index + 1] if part in SECOND_MESH else []):
            row = model.rows[ri]
            if row.vert is not None:
                yield from zip(row.materials, row.face_uvs)


def budget(raw, fighter, cap):
    r = records(fighter)
    model = parse_kmd((raw / f"{r['kmd']:03}.bin").read_bytes())
    vram, _ = load_character_vram((raw / f"{r['arc']:03}.bin").read_bytes())
    textures, luts, reduced, faces, reached = {}, set(), 0, 0, 0
    reached8 = box = 0
    for material, uv in faces_of(model):
        width, height, codes, lut, opaque, cut = bake_face_lut4(vram, material, uv, cap)
        got, area = texels_reached(uv, width, height)
        reached, reached8, box = reached + got, reached8 + texels_reached(uv, 8, 8)[0], box + area
        textures[(width, height, tuple(codes), lut, opaque)] = width * height // 2
        luts.add(lut)
        reduced += cut
        faces += 1
    return dict(faces=faces, textures=len(textures), pixel_bytes=sum(textures.values()), luts=len(luts),
                lut_bytes=32 * len(luts), reduced_faces=reduced,
                # Source texels sampled, against the faces' UV boxes (a box overstates a triangle's texels).
                texels_native=reached, texels_8x8=reached8, texels_box=box)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--raw', type=Path, required=True)
    p.add_argument('--caps', type=int, nargs='+', default=[16, 24, 32])
    p.add_argument('--only', type=int, nargs='+')
    args = p.parse_args()
    for fighter in ROSTER:
        if args.only and fighter.id not in args.only:
            continue
        row = {'id': fighter.id, 'name': fighter.name}
        for cap in args.caps:
            row[f'cap{cap}'] = budget(args.raw, fighter, cap)
        print(json.dumps(row), flush=True)


if __name__ == '__main__':
    main()
