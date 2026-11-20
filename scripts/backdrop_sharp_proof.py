#!/usr/bin/env python3
"""Screen-space proof for sharper stage backdrops (docs/STAGE_BACKDROP_SHARPNESS_PLAN.md, step 1).

Renders eight cameras through today's picture (1024 x 240, 255 colours) and
candidates (--candidates WxH,..., 16 palettes of 15 colours per 8 x 8 cell), each
through the real line mapping (t3_stage_backdrop_eval) and a nearest-texel read
like the VDP2's, and compares both with the true box ray cast at 320 x 224:

  * mean absolute error per dot and channel (8-bit levels);
  * gradient energy of the shown picture over the truth's (1 is as sharp;
    below 1 is blurred, above 1 is blocky edges).

The registered error lets each 16 x 16 block slide up to 12 x 4 dots to its best match. The blurred error compares both through a 5 x 5 mean, so that the line scroll's own
misregistration (several texels, PAUL_STAGE_VDP2.md) does not dominate. The cameras are synthetic, fight-like views (distance 4400-5500, height 600-1600
above the midpoint, projection distance 500, the panorama turns of the golden
table in PAUL_STAGE_VDP2.md) looking 300 units above the midpoint; the dots
the floor hides are left out. With
--images each camera writes truth | today | candidate. Reads and writes private data.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from backdrop_format_study import SIZES, cache_path, channels, format_a, global_255  # noqa: E402
from build_stage_packs import BAKE_HEIGHT, TAN_TOP, box_filter, read_arc, stage_metadata  # noqa: E402
from check_backdrop_mapping import HEIGHT, ONE, PSX_ROW1, WIDTH, to_local, unit  # noqa: E402
from panorama_raycast import Panorama  # noqa: E402

TURNS = (0, 190, 321, 141, 43, 54, 45, 24)


def cameras():
    """(turn, eye in 1/96 PS1 units relative to the midpoint, view_proj rows, axes) per camera."""
    out = []
    for i, turn in enumerate(TURNS):
        yaw = np.radians(-20 + 40*((i*3) % 8)/7)
        distance, height, h = 4400 + 150*i, 600 + 140*i, 500.0
        # PS1 axes, y down: the eye is `height` above the midpoint and looks 300 above it.
        eye_ps1 = np.array([distance*np.sin(yaw), -height, -distance*np.cos(yaw)])
        forward_ps1 = unit(np.array([0.0, -300.0, 0.0]) - eye_ps1)
        forward = np.array([forward_ps1[0], -forward_ps1[1], -forward_ps1[2]])
        right = unit(np.cross(forward, np.array([0.0, 1.0, 0.0])))
        up = np.cross(right, forward)
        fx, fy = h/184, PSX_ROW1*h/(240*4096)
        m = np.zeros(16, dtype=np.int64)
        m[0:3] = np.rint(fx*right*ONE)
        m[4:7] = np.rint(fy*up*ONE)
        m[12:15] = np.rint(forward*ONE)
        eye = np.array([eye_ps1[0]*96, -eye_ps1[1]*96, -eye_ps1[2]*96], dtype=np.int64)
        out.append((turn, eye, m, (m[0:3]/ONE/fx, m[4:7]/ONE/fy, m[12:15]/ONE, fx, fy)))
    return out


def candidate_picture(raw, size, supersample, kind):
    """The picture as the VDP2 would show it: 8-bit RGB per texel, black where transparent."""
    columns, rows, _ = size
    colour = box_filter(raw.astype(np.int64), columns, rows, supersample).reshape(rows, columns)
    opaque = colour != 0
    rgb = channels(colour)
    if kind == 'a':
        shown = format_a(rgb, opaque, rows, columns)[2]
    else:
        index, _, palette = global_255(rgb, opaque)
        shown = np.where(opaque[..., None], palette[np.maximum(index - 1, 0)], 0)
    return (shown*255//31).astype(np.uint8), opaque


def show(entries, picture, columns, rows):
    sx = (entries[:, 0][:, None] + np.arange(WIDTH)[None, :]*entries[:, 2][:, None]).reshape(-1)
    sy = np.repeat(entries[:, 1], WIDTH)
    column = ((sx/ONE) % columns).astype(int)
    row = (sy/ONE)
    inside = (row >= 0) & (row < rows)
    out = np.zeros((len(sx), 3), np.uint8)
    out[inside] = picture[row[inside].astype(int), column[inside]]
    return out, inside


def blurred(image, size=5):
    """size x size box mean of an H x W x 3 image (zero padded), as float."""
    pad = size//2
    padded = np.pad(image.astype(np.float64), ((pad + 1, pad), (pad + 1, pad), (0, 0)))
    total = padded.cumsum(0).cumsum(1)
    h, w = image.shape[:2]
    return (total[size:size + h, size:size + w] - total[:h, size:size + w]
            - total[size:size + h, :w] + total[:h, :w])/(size*size)


def registered_error(image, truth, visible, block=16, reach=(12, 4)):
    """Mean absolute error per visible dot when each block of the shown image may slide up to
    `reach` (dots across, down) to its best match: what is left once the line scroll's
    placement error (several texels, PAUL_STAGE_VDP2.md) is set aside."""
    h, w = visible.shape
    best = np.full((h//block, w//block), np.inf)
    t = truth.astype(np.float64)
    weight = visible.reshape(h//block, block, w//block, block).sum(axis=(1, 3))
    for dy in range(-reach[1], reach[1] + 1):
        for dx in range(-reach[0], reach[0] + 1):
            moved = np.roll(image.astype(np.float64), (dy, dx), axis=(0, 1))
            diff = (np.abs(moved - t).mean(-1)*visible).reshape(h//block, block, w//block, block).sum(axis=(1, 3))
            best = np.minimum(best, diff)
    return float(best.sum()/max(1, weight.sum()))


def energy(image, mask):
    grey = image.astype(np.float64).mean(-1)
    gx = np.diff(grey, axis=1)[:-1] ** 2 + np.diff(grey, axis=0)[:, :-1] ** 2
    both = (mask[:-1, :-1] & mask[1:, 1:] & mask[:-1, 1:] & mask[1:, :-1])
    return float(gx[both].mean())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--cache', type=Path, required=True)
    p.add_argument('--eval', type=Path, required=True, help='t3_stage_backdrop_eval')
    p.add_argument('--stages', required=True)
    p.add_argument('--candidates', default='2048x240,2048x352', help='WxH list, from the baked sizes')
    p.add_argument('--supersample', type=int, default=2)
    p.add_argument('--images', type=Path)
    a = p.parse_args()
    old_size = SIZES[0]
    new_sizes = [next(s for s in SIZES if f'{s[0]}x{s[1]}' == c) for c in a.candidates.split(',')]
    views = cameras()
    xs, ys = np.meshgrid(np.arange(WIDTH), np.arange(HEIGHT))
    ndc_x, ndc_y = (2*xs.reshape(-1) - WIDTH)/WIDTH, (HEIGHT - 2*ys.reshape(-1))/HEIGHT
    summary = []
    for stage in (int(s) for s in a.stages.split(',')):
        tmd = (a.source/f'raw/{56 + stage:03}.bin').read_bytes()
        height, _ = stage_metadata((a.source/'SLUS_004.02').read_bytes(), stage)
        vram, _, _ = read_arc((a.source/f'raw/{36 + stage:03}.bin').read_bytes())
        scene = Panorama(tmd, vram, height)
        half = round(scene.half)
        shown = {}
        for name, size, kind in [('today', old_size, 'b')] + [(f'{s[0]}x{s[1]}', s, 'a') for s in new_sizes]:
            raw = np.load(cache_path(a.cache, stage, size, a.supersample))
            picture, _ = candidate_picture(raw, size, a.supersample, kind)
            query = [f"{half} {BAKE_HEIGHT} {round(TAN_TOP*ONE)} {round(size[2]*ONE)} {size[0]} {size[1]} "
                     f"{WIDTH} {HEIGHT} 1"]
            query += [' '.join(str(int(v)) for v in [t, *eye, *m]) for t, eye, m, _ in views]
            out = subprocess.run([str(a.eval)], input='\n'.join(query)+'\n', capture_output=True, text=True, check=True)
            shown[name] = (picture, size, out.stdout.splitlines())
        for k, (turn, eye, m, (right, up, forward, fx, fy)) in enumerate(views):
            dirs = to_local(forward[None, :] + (ndc_x/fx)[:, None]*right[None, :] + (ndc_y/fy)[:, None]*up[None, :], turn)
            origin = to_local((eye/96.0)[None, :], turn)[0]
            t, colour, _ = scene.cast(origin, dirs)
            with np.errstate(divide='ignore', invalid='ignore'):
                floor = np.where(dirs[:, 1] > 0, -origin[1]/dirs[:, 1], np.inf)
            visible = np.isfinite(t) & (t < floor) & (colour != 0)
            truth = (channels(colour)*255//31).astype(np.uint8)
            truth[~visible] = 0
            row = dict(stage=stage, camera=k, turn=turn, visible=int(visible.sum()))
            panels = [truth.reshape(HEIGHT, WIDTH, 3)]
            for name, (picture, size, lines) in shown.items():
                fields = lines[k].split()
                if fields[0] != 'ok':
                    row[name] = 'fail'
                    continue
                entries = np.array(fields[1:], dtype=np.int64).reshape(HEIGHT, 4)
                image, inside = show(entries, picture, size[0], size[1])
                image[~visible] = 0
                mask = (visible & inside).reshape(HEIGHT, WIDTH)
                err = np.abs(image.astype(int) - truth.astype(int))[visible].mean()
                # Misregistration by the line scroll counts fully above; the blurred error
                # compares what the eye sees at that scale (colour and layout).
                soft = np.abs(blurred(image.reshape(HEIGHT, WIDTH, 3)) -
                              blurred(truth.reshape(HEIGHT, WIDTH, 3))).reshape(-1, 3)[visible].mean()
                near = registered_error(image.reshape(HEIGHT, WIDTH, 3), truth.reshape(HEIGHT, WIDTH, 3),
                                        visible.reshape(HEIGHT, WIDTH))
                row[name] = dict(error=round(float(err), 3), blurred_error=round(float(soft), 3),
                                 registered_error=round(near, 3),
                                 sharpness=round(energy(image.reshape(HEIGHT, WIDTH, 3), mask)/
                                                 energy(truth.reshape(HEIGHT, WIDTH, 3), visible.reshape(HEIGHT, WIDTH)), 3))
                panels.append(image.reshape(HEIGHT, WIDTH, 3))
            summary.append(row)
            print(json.dumps(row), flush=True)
            if a.images:
                from PIL import Image
                a.images.mkdir(parents=True, exist_ok=True)
                Image.fromarray(np.concatenate(panels, axis=1)).save(a.images/f'stage{stage:02}_cam{k}.png')


if __name__ == '__main__':
    main()
