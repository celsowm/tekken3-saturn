#!/usr/bin/env python3
"""Inspect/export private title-overlay TIMs using Tekken3Remake research tools.

The imported overlay_images and system_textures tools are by n0madic under
PolyForm Noncommercial 1.0.0; see third_party/Tekken3Remake and NOTICE.md.
Output contains retail game data and must remain in assets/imported.
"""
import argparse
import struct
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
from overlay_images import scan, describe, to_png
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--overlay', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    if not any(root.resolve() in out.parents for root in (ROOT / 'assets/imported', ROOT / 'build')):
        parser.error('output must be under assets/imported or build')
    out.mkdir(parents=True, exist_ok=True)
    for found in scan(args.overlay.read_bytes(), 0x800B8D58):
        if found.archive and len(found.tims) == 15:
            for i, tim in enumerate(found.tims):
                print(i, describe(tim))
                to_png(tim, out / f'title-{i:02}.png')
            picture = Image.new('RGB', (368, 480))
            for i in range(6):
                picture.paste(Image.open(out / f'title-{i:02}.png'),
                              ((i % 3) * 128, 0 if i < 3 else 256))
            picture.save(out / 'title.png')
            artwork = Image.new('RGB', (136, 216))
            artwork.paste(picture.resize((136, 200), Image.Resampling.LANCZOS), (0, 0))
            artwork.paste(Image.open(out / 'title-06.png'), (0, 200))
            artwork = artwork.quantize(colors=256, dither=Image.Dither.NONE)
            palette = artwork.getpalette()
            colors = []
            for i in range(256):
                r, g, b = palette[3*i:3*i+3]
                colors.append(0x8000 | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10))
            (out / 'TITLE.BIN').write_bytes(struct.pack('>256H', *colors) +
                artwork.crop((0, 0, 136, 200)).tobytes() +
                artwork.crop((0, 200, 72, 216)).tobytes())
            return
    parser.error('title image archive was not found')


if __name__ == '__main__':
    main()
