#!/usr/bin/env python3
"""Pack Tekken Ball's HOW TO picture (volley.ovl FUN_800B4088, the pause menu's fourth page) for BALL.BIN.

The input is how_to.png from scripts/export_ball_stage_pictures.py (retail-derived, private). HOWTO.BIN holds 256 RGB555
big-endian palette words (index 0 is the page's white), then 192 x 102 palette indices. Output stays under build/ or
assets/imported/.

    scripts/pack_ball_howto.py --picture assets/imported/ball-probe/stage-usa/stages/v/how_to.png --out build/generated/private_ball
"""
import argparse
import struct
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE_W, SOURCE_H = 256, 204
W, H = 192, 102      # the page shows it at about 188 x 102 screen pixels (the HUD's x * 20 / 23, y / 2)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--picture', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    out = args.out.resolve()
    if not any(root.resolve() in out.parents or root.resolve() == out for root in (ROOT / 'assets/imported', ROOT / 'build')):
        p.error('output must be under assets/imported or build')
    image = Image.open(args.picture).convert('RGBA')
    if image.size != (SOURCE_W, SOURCE_H):
        p.error(f'expected a {SOURCE_W}x{SOURCE_H} picture, got {image.size}')
    page = Image.new('RGB', image.size, (255, 255, 255))
    page.paste(image, mask=image.getchannel('A'))      # transparent texels show the page's white
    page = page.resize((W, H), Image.LANCZOS)
    indexed = page.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    flat = indexed.getpalette()[:768] + [255] * (768 - len(indexed.getpalette()[:768]))
    words = [(flat[3 * i + 2] >> 3) << 10 | (flat[3 * i + 1] >> 3) << 5 | flat[3 * i] >> 3 for i in range(256)]
    out.mkdir(parents=True, exist_ok=True)
    (out / 'HOWTO.BIN').write_bytes(struct.pack('>256H', *words) + indexed.tobytes())
    print(out / 'HOWTO.BIN')


if __name__ == '__main__':
    main()
