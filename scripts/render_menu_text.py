#!/usr/bin/env python3
"""Render a string with one of the original system fonts to a PNG, to check glyph extraction.

    scripts/render_menu_text.py --font 0 --clut 6 --scale 4 --out build/t.png "NAMCO PRESENTS."

Fonts 0-2 are small, medium and large; --clut is the CLUT row (colour) of the font palette.
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import pack_menu_art as art  # noqa: E402
from PIL import Image  # noqa: E402


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('text')
    p.add_argument('--font', type=int, default=0)
    p.add_argument('--clut', type=int, default=6)
    p.add_argument('--scale', type=int, default=4)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    args = p.parse_args()
    fonts = art.Fonts(args.exe.read_bytes())
    glyphs = fonts.render(args.text, args.font)
    palette = fonts.palette(args.clut)
    image = Image.new('RGB', (glyphs.shape[1], glyphs.shape[0]), (0, 0, 0))
    for y in range(glyphs.shape[0]):
        for x in range(glyphs.shape[1]):
            index = int(glyphs[y, x])
            if index and palette[index]:
                image.putpixel((x, y), art.rgb555(palette[index]))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    image.resize((image.width * args.scale, image.height * args.scale), Image.Resampling.NEAREST).save(args.out)
    print(args.out, glyphs.shape)


if __name__ == '__main__':
    main()
