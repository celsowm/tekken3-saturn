#!/usr/bin/env python3
"""Tile a run's frame-N.png screenshots into one labelled contact sheet.

    scripts/screenshot_sheet.py RUN_DIR OUT.png [--columns 6] [--width 240] [--first N] [--last N]
"""
import argparse
import re
from pathlib import Path

from PIL import Image, ImageDraw


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('run', type=Path)
    p.add_argument('out', type=Path)
    p.add_argument('--columns', type=int, default=6)
    p.add_argument('--width', type=int, default=240)
    p.add_argument('--first', type=int, default=0)
    p.add_argument('--last', type=int, default=1 << 30)
    args = p.parse_args()
    shots = sorted((int(m.group(1)), path) for path in args.run.glob('frame-*.png')
                   if (m := re.fullmatch(r'frame-(\d+)\.png', path.name)) and args.first <= int(m.group(1)) <= args.last)
    if not shots:
        raise SystemExit('no frame-N.png screenshots in range')
    tiles = []
    for field, path in shots:
        image = Image.open(path).convert('RGB')
        image = image.resize((args.width, args.width * image.height // image.width))
        ImageDraw.Draw(image).text((4, 4), str(field), fill=(255, 255, 0))
        tiles.append(image)
    height = max(tile.height for tile in tiles)
    rows = (len(tiles) + args.columns - 1) // args.columns
    sheet = Image.new('RGB', (args.columns * args.width, rows * height))
    for i, tile in enumerate(tiles):
        sheet.paste(tile, (i % args.columns * args.width, i // args.columns * height))
    sheet.save(args.out)
    print(f'{len(tiles)} screenshots -> {args.out}')


if __name__ == '__main__':
    main()
