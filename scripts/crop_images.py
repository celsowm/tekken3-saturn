#!/usr/bin/env python3
"""Crop one box from several screenshots and stack the crops in one image.

Reading the HUD of many recordings then takes one image read. The default
box is the 704x480 Mednafen capture's ROUND/TIME/FRAME rows.

    scripts/crop_images.py OUT.png A.png B.png ... [--box X Y W H]
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('out', type=Path)
    p.add_argument('images', type=Path, nargs='+')
    p.add_argument('--box', type=int, nargs=4, default=(20, 80, 420, 50), metavar=('X', 'Y', 'W', 'H'))
    p.add_argument('--label-width', type=int, default=260)
    args = p.parse_args()
    x, y, w, h = args.box
    sheet = Image.new('RGB', (args.label_width + w, h * len(args.images)), 'white')
    draw = ImageDraw.Draw(sheet)
    for i, path in enumerate(args.images):
        sheet.paste(Image.open(path).convert('RGB').crop((x, y, x + w, y + h)), (args.label_width, i * h))
        draw.text((4, i * h + h // 3), f'{path.parent.name}/{path.stem}', fill='black')
    sheet.save(args.out)


if __name__ == '__main__':
    main()
