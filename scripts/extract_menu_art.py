#!/usr/bin/env python3
"""Dump the private select.ovl / title.ovl picture archives as PNG for review.

Input is a raw BNS record exported by scripts/psx_disc_import.py
(`--extract-ids 9,10`): record 9 is select.ovl, record 10 is title.ovl.
Archives are found structurally with Tekken3Remake's overlay_images
(n0madic, PolyForm Noncommercial 1.0.0; see NOTICE.md).
Output contains retail game data and must stay under assets/imported or build.
"""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))
from overlay_images import scan, describe, to_png
from PIL import Image


def sheet(paths, out, cell=160):
    """Contact sheet: every picture fitted into a cell, 8 per row."""
    cols = 8
    rows = (len(paths) + cols - 1) // cols
    img = Image.new('RGB', (cols * cell, rows * cell), (40, 40, 48))
    for i, p in enumerate(paths):
        pic = Image.open(p).convert('RGB')
        pic.thumbnail((cell - 4, cell - 4))
        img.paste(pic, ((i % cols) * cell + 2, (i // cols) * cell + 2))
    img.save(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record', type=Path, required=True, help='raw BNS record')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--min-members', type=int, default=3)
    args = parser.parse_args()
    out = args.out.resolve()
    if not any(root.resolve() in out.parents for root in (ROOT / 'assets/imported', ROOT / 'build')):
        parser.error('output must be under assets/imported or build')
    out.mkdir(parents=True, exist_ok=True)
    data = args.record.read_bytes()
    for number, found in enumerate(f for f in scan(data, 0x80100000)
                                   if f.archive and len(f.tims) >= args.min_members):
        paths = []
        for i, tim in enumerate(found.tims):
            path = out / f'a{number}-{i:02}.png'
            print(f'a{number}-{i:02}', describe(tim))
            try:
                to_png(tim, path)
                if path.exists():           # palette-only members write nothing
                    paths.append(path)
            except Exception as error:      # palette-only members have no picture
                print('  skipped:', error)
        if paths:
            sheet(paths, out / f'a{number}-sheet.png')


if __name__ == '__main__':
    main()
