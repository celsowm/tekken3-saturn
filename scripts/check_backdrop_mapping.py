"""Check the backdrop line mapping against the private panorama geometry.

For cameras dumped by t3_match_camera_eval (one JSON line per frame: eye
around the fighters' midpoint and the Q12 view rows, PS1 axes), this builds
the Saturn view_proj as src/saturn/move_probe.c does, runs
t3_stage_backdrop_eval for the line scroll entries, and compares every dot
of the scrolled cylinder with the panorama ray cast from the real eye:

  - position: where the dot samples the cylinder against where the bake point
    sees the true hit (texels; one column is about 2.7 dots, one row 2);
  - colour: share of dots whose sampled colour differs from the true one.

Dots whose ray meets the floor before a wall are left out (the floor layer
covers them). With --images, each camera writes truth | cylinder | difference
to the output directory. Everything read and written is private data.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_local_paul_stage import read_arc  # noqa: E402
from panorama_raycast import Panorama  # noqa: E402

WIDTH, HEIGHT = 320, 224
ONE = 65536
PSX_ROW1 = 0x1BD2


def unit(v):
    return v/np.linalg.norm(v)


def saturn_camera(cam):
    """(view_proj rows 0, 1, 3 as 16.16 ints, eye fx16 ints, axes and focal terms)."""
    ex, ey, ez = cam['eye']
    r = np.array(cam['rows'], dtype=np.float64)
    h = cam['h']
    eye = np.array([ex*96, -ey*96, -ez*96], dtype=np.int64)
    forward = unit(np.array([r[2, 0], -r[2, 1], -r[2, 2]]))
    up_hint = np.array([-r[1, 0], r[1, 1], r[1, 2]])
    right = unit(np.cross(forward, up_hint))
    up = np.cross(right, forward)
    fx, fy = h/184, PSX_ROW1*h/(240*4096)
    m = np.zeros(16, dtype=np.int64)
    m[0:3] = np.rint(fx*right*ONE)
    m[4:7] = np.rint(fy*up*ONE)
    m[12:15] = np.rint(forward*ONE)
    return m, eye, (m[0:3]/ONE/fx, m[4:7]/ONE/fy, m[12:15]/ONE, fx, fy)


def to_local(v, turn):
    """Saturn world vectors (n, 3) to the turned panorama's PS1 frame."""
    a = 2*np.pi*turn/4096
    c, s = np.cos(a), np.sin(a)
    x, y, z = v[:, 0], -v[:, 1], -v[:, 2]
    return np.stack([x*c + z*s, y, -x*s + z*c], axis=1)


def bake_view(points, bake):
    """Cylinder column and row the bake point sees at local points."""
    column = (np.arctan2(points[:, 0], points[:, 2])/(2*np.pi) % 1.0)*bake['columns']
    tan = (-points[:, 1] - bake['bake_height'])/np.hypot(points[:, 0], points[:, 2])
    return column, (bake['tan_top'] - tan)*bake['rows_per_tan']


def rgb(colour):
    return np.stack([((colour >> (5*k)) & 31)*255//31 for k in range(3)], axis=-1).astype(np.uint8)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--source', type=Path, required=True, help='private import (tekken3-usa)')
    p.add_argument('--stage', type=Path, required=True, help='build_local_paul_stage.py output')
    p.add_argument('--cameras', type=Path, required=True, help='t3_match_camera_eval dump')
    p.add_argument('--eval', type=Path, required=True, help='t3_stage_backdrop_eval')
    p.add_argument('--count', type=int, default=8, help='cameras, spread over the dump')
    p.add_argument('--images', type=Path, help='write PNGs here')
    p.add_argument('--compare', type=Path, help='an earlier report: print the change of each figure to stderr')
    a = p.parse_args()

    manifest = json.loads((a.stage/'manifest.json').read_text())
    bake = manifest['cylinder']
    stg = (a.stage/'PAUL.STG').read_bytes()
    header = struct.unpack_from('>16I', stg)
    columns, rows = header[2], header[3]
    palette = np.array(struct.unpack_from(f'>{header[11]}H', stg, header[10]), dtype=np.int64)
    picture = np.frombuffer(stg, np.uint8, header[13], header[12]).reshape(rows, columns)
    half = manifest['panorama_half']

    vram, _, _ = read_arc((a.source/'raw/036.bin').read_bytes())
    scene = Panorama((a.source/'raw/056.bin').read_bytes(), vram, manifest['panorama_height'])

    cameras = [json.loads(line) for line in a.cameras.read_text().splitlines() if line.strip()]
    picks = [cameras[round(i*(len(cameras)-1)/max(1, a.count-1))] for i in range(a.count)]
    built = [saturn_camera(cam) for cam in picks]
    query = [f"{half} {bake['bake_height']} {round(bake['tan_top']*ONE)} {round(bake['rows_per_tan']*ONE)} "
             f"{columns} {rows} {WIDTH} {HEIGHT} 1"]
    for cam, (m, eye, _) in zip(picks, built):
        query.append(' '.join(str(int(v)) for v in [cam['turn'], *eye, *m]))
    out = subprocess.run([str(a.eval)], input='\n'.join(query)+'\n', capture_output=True, text=True, check=True)
    results = out.stdout.splitlines()
    if len(results) != len(picks):
        raise SystemExit('eval tool returned %d lines for %d cameras' % (len(results), len(picks)))

    xs, ys = np.meshgrid(np.arange(WIDTH), np.arange(HEIGHT))
    ndc_x = (2*xs.reshape(-1) - WIDTH)/WIDTH
    ndc_y = (HEIGHT - 2*ys.reshape(-1))/HEIGHT
    if a.images:
        a.images.mkdir(parents=True, exist_ok=True)
        from PIL import Image
    report = []
    for cam, (m, eye, (right, up, forward, fx, fy)), line in zip(picks, built, results):
        fields = line.split()
        if fields[0] != 'ok':
            report.append(dict(frame=cam['frame'], result='fail'))
            continue
        entries = np.array(fields[1:], dtype=np.int64).reshape(HEIGHT, 4)
        dirs = to_local(forward[None, :] + (ndc_x/fx)[:, None]*right[None, :] + (ndc_y/fy)[:, None]*up[None, :],
                        cam['turn'])
        origin = to_local((eye/96.0)[None, :], cam['turn'])[0]
        t, colour, points = scene.cast(origin, dirs)
        # The floor (PS1 y = 0, y down) in front of the walls hides the panorama there.
        with np.errstate(divide='ignore', invalid='ignore'):
            floor = np.where(dirs[:, 1] > 0, -origin[1]/dirs[:, 1], np.inf)
        visible = np.isfinite(t) & (t < floor) & (colour != 0)
        true_column, true_row = bake_view(points, bake)
        # What the scrolled layer shows: column x + dot * zoom and, as each
        # entry replaces the vertical coordinate (one entry a line), row y.
        sx = (entries[:, 0][:, None] + np.arange(WIDTH)[None, :]*entries[:, 2][:, None]).reshape(-1)
        sy = (entries[:, 1][:, None] + 0*xs).reshape(-1)
        column = (sx/ONE) % columns
        row = sy/ONE
        inside = (row >= 0) & (row < rows)
        shown = np.zeros(len(sx), dtype=np.int64)
        shown[inside] = palette[picture[row[inside].astype(int), column[inside].astype(int)]]
        dc = np.abs((column - true_column + columns/2) % columns - columns/2)
        dr = np.abs(row - true_row)
        v = visible & inside
        on_wall = np.maximum(np.abs(points[:, 0]), np.abs(points[:, 2])) > 0.98*half
        # The truth through the same palette, so quantization does not count as a miss.
        levels = rgb(palette[1:]).astype(np.int64)
        def nearest(c):
            q = rgb(c).astype(np.int64)
            return palette[1:][((q[:, None, :] - levels[None, :, :])**2).sum(2).argmin(1)]
        truth_q = np.zeros_like(colour)
        truth_q[visible] = nearest(colour[visible])
        differ = v & (shown != truth_q)
        entry = dict(frame=cam['frame'], turn=cam['turn'], eye=cam['eye'], visible_dots=int(v.sum()),
                     column_texels=[round(float(np.percentile(dc[v], q)), 3) for q in (50, 95, 100)] if v.any() else None,
                     row_texels=[round(float(np.percentile(dr[v], q)), 3) for q in (50, 95, 100)] if v.any() else None,
                     colour_mismatch=round(float(differ.sum()/max(1, v.sum())), 4),
                     # The mapping assumes the picture lies on the box walls.
                     off_wall_share=round(float((v & ~on_wall).sum()/max(1, v.sum())), 4),
                     on_wall_texels=[round(float(np.percentile(np.hypot(dc, dr)[v & on_wall], 95)), 3)]
                     if (v & on_wall).any() else None,
                     off_wall_texels=[round(float(np.percentile(np.hypot(dc, dr)[v & ~on_wall], 95)), 3)]
                     if (v & ~on_wall).any() else None,
                     uncovered=int((visible & ~inside).sum()))
        # The floor of any line scroll: per line the best affine column fit
        # (least squares in dots) and the best single row.
        best = np.full(len(sx), np.nan)
        tc, tr, vv = true_column.reshape(HEIGHT, WIDTH), true_row.reshape(HEIGHT, WIDTH), v.reshape(HEIGHT, WIDTH)
        for y in range(HEIGHT):
            k = vv[y]
            if k.sum() < 2:
                continue
            c = np.unwrap(tc[y, k]*2*np.pi/columns)*columns/(2*np.pi)
            fit = np.polyval(np.polyfit(np.arange(WIDTH)[k], c, 1), np.arange(WIDTH)[k])
            best.reshape(HEIGHT, WIDTH)[y, k] = np.hypot(c - fit, tr[y, k] - np.median(tr[y, k]))
        entry['line_scroll_floor_texels'] = [round(float(np.nanpercentile(best, q)), 3) for q in (50, 95)] \
            if np.isfinite(best).any() else None
        report.append(entry)
        if a.images:
            truth_img = np.where(visible[:, None], rgb(colour), 0).reshape(HEIGHT, WIDTH, 3)
            shown_img = np.where((visible & inside)[:, None], rgb(shown), 0).reshape(HEIGHT, WIDTH, 3)
            diff = np.abs(truth_img.astype(int) - shown_img.astype(int)).clip(0, 63)*4
            Image.fromarray(np.concatenate([truth_img, shown_img, diff.astype(np.uint8)], axis=1)).save(
                a.images/f"frame{cam['frame']:05d}.png")
    print(json.dumps(report, indent=1))
    if a.compare:
        before = {r['frame']: r for r in json.loads(a.compare.read_text())}
        keys = ('column_texels', 'row_texels', 'on_wall_texels', 'colour_mismatch')
        for r in report:
            old = before.get(r['frame'], {})
            print(f"frame {r['frame']}: " + '; '.join(f"{k} {old.get(k)} -> {r.get(k)}" for k in keys),
                  file=sys.stderr)


if __name__ == '__main__':
    main()
