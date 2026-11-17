#!/usr/bin/env python3
"""Pack the spinning ball of Tekken Ball's type page (the original's select screen shows it turning) for MENU.BIN.

Each ball type is rendered from the private ball data (scripts/extract_ball_data.py: the 42-vertex mesh, the type's eight triangle
and eight quad colours, its specular power and back colour) with the same lighting as the fight (src/game/ball_light.c, the default
stage rig), turned about its pole axis in FRAMES steps, flat per face, then shrunk to the menu's anamorphic texels (the menu draws
them three times as wide as high). BALLSPIN.BIN holds, per type: PALETTE_WORDS RGB555 big-endian words (code 0 is transparent, so the
first word is for texel value 1), then FRAMES * W * H texels. Retail-derived: it stays under build/.

    scripts/pack_ball_spin.py --data assets/imported/ball-probe/ball-data.bin --out build/generated/private_ball
"""
import argparse
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
FRAMES, W, H, PALETTE_WORDS, SUPER = 8, 40, 70, 92, 5
RIG_LIGHT, RIG_COLOUR = (-2048, -2896, -2048), 200
HEAD, OPTION_WORDS = 6, 12


def clamp15(v):
    return max(0, min(0x7FFF, v))


def light(normal, back, power):
    """src/game/ball_light.c t3_ball_light_vertex: 4096 = 1."""
    ir0 = clamp15(sum(2 * l * n for l, n in zip(RIG_LIGHT, normal)) >> 12)
    ir = [ir0, clamp15(normal[1]), 0]
    for _ in range(max(power, 1)):
        ir = [clamp15(v * v >> 12) for v in ir]
    return [clamp15((b << 4) + (((RIG_COLOUR << 4) * ir[0] + ((RIG_COLOUR >> 1) << 4) * ir[1]) >> 12)) for b in back]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--data', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    out = args.out.resolve()
    if not any(root.resolve() in out.parents or root.resolve() == out for root in (ROOT / 'assets/imported', ROOT / 'build')):
        p.error('output must be under assets/imported or build')
    d = args.data.read_bytes()
    verts = HEAD + 3 * OPTION_WORDS * 2 + 4 * 6
    normals = verts + 42 * 6
    tris = normals + 42 * 6
    quads = tris + 16 * 3
    colours = quads + 32 * 4
    vertex = [struct.unpack('>3h', d[verts + 6 * i:verts + 6 * i + 6]) for i in range(42)]
    normal = [struct.unpack('>3h', d[normals + 6 * i:normals + 6 * i + 6]) for i in range(42)]
    faces = [tuple(d[tris + 3 * i:tris + 3 * i + 3]) for i in range(16)] + [tuple(d[quads + 4 * i:quads + 4 * i + 4]) for i in range(32)]
    # a quad's corners are in strip order (0 1 / 2 3); a polygon takes them around
    faces = [f if len(f) == 3 else (f[0], f[1], f[3], f[2]) for f in faces]
    blob = bytearray()
    tilt = math.radians(25)
    for kind in range(3):
        row = d[HEAD + kind * OPTION_WORDS * 2:HEAD + (kind + 1) * OPTION_WORDS * 2]
        w6, w7 = row[12] << 8 | row[13], row[14] << 8 | row[15]
        power, back = w6 & 255, (w6 >> 8, w7 & 255, w7 >> 8)
        face_colour = []
        for i in range(16):
            c = struct.unpack('>I', d[colours + 64 * kind + 4 * i:colours + 64 * kind + 4 * i + 4])[0]
            face_colour.append((c & 255, c >> 8 & 255, c >> 16 & 255))
        sheet = []
        for frame in range(FRAMES):
            a = 2 * math.pi * frame / FRAMES
            ca, sa, ct, st = math.cos(a), math.sin(a), math.cos(tilt), math.sin(tilt)

            def turn(v):
                x, y, z = v
                x, z = x * ca - z * sa, x * sa + z * ca              # about the pole axis (y)
                return x, y * ct - z * st, y * st + z * ct           # tilted towards the viewer
            pos = [turn(v) for v in vertex]
            nrm = [turn(n) for n in normal]
            lit = [light([int(c) for c in n], back, power) for n in nrm]
            size = 2 * SUPER * 160
            image = Image.new('RGBA', (size, size), (0, 0, 0, 0))
            draw = ImageDraw.Draw(image)
            scale = size / 2 / 400
            order = sorted(range(len(faces)), key=lambda i: -sum(pos[v][2] for v in faces[i]))
            for i in order:
                f = faces[i]
                if sum(nrm[v][2] for v in f) >= 0:                    # turned away from the viewer (z towards him is negative)
                    continue
                slot = (i & 7) if i < 16 else 8 + ((i - 16) & 7)
                mean = [sum(lit[v][ch] for v in f) / len(f) for ch in range(3)]
                rgb = tuple(min(255, int(face_colour[slot][ch] * mean[ch] / 4096)) for ch in range(3))
                draw.polygon([(size / 2 + pos[v][0] * scale, size / 2 + pos[v][1] * scale) for v in f], fill=rgb + (255,))
            sheet.append(image.resize((W, H), Image.LANCZOS))
        stack = Image.new('RGB', (W, H * FRAMES))
        alpha = np.zeros((H * FRAMES, W), bool)
        for n, image in enumerate(sheet):
            stack.paste(image.convert('RGB'), (0, n * H))
            alpha[n * H:(n + 1) * H] = np.asarray(image.getchannel('A')) >= 128
        indexed = stack.quantize(colors=PALETTE_WORDS - 1, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        palette = indexed.getpalette()[:3 * (PALETTE_WORDS - 1)]
        palette += [0] * (3 * (PALETTE_WORDS - 1) - len(palette))
        words = [(palette[3 * i + 2] >> 3) << 10 | (palette[3 * i + 1] >> 3) << 5 | palette[3 * i] >> 3 for i in range(PALETTE_WORDS - 1)]
        pixels = np.where(alpha, np.asarray(indexed).astype(np.int32) + 1, 0).astype(np.uint8)
        blob += struct.pack(f'>{PALETTE_WORDS}H', *words, 0) + pixels.tobytes()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'BALLSPIN.BIN').write_bytes(bytes(blob))
    print(out / 'BALLSPIN.BIN', len(blob))


if __name__ == '__main__':
    main()
