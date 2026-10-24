#!/usr/bin/env python3
"""Compare USA EXE table spans with the same spans in the Japan Rev.1 EXE.

Each span is a USA PS-X EXE file offset and a size (as in the private
pose-table manifests); the Rev.1 address comes from the layout's matched
points (fight_oracle_export.Layout). Prints only offsets and differing word
indices, never table bytes.
"""
import argparse
import struct
from pathlib import Path

from fight_oracle_export import LAYOUT, Layout

ROOT = Path(__file__).resolve().parents[1]
BASE = 0x80010000


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--usa-exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    p.add_argument('--jp-exe', type=Path, default=ROOT / 'third_party/Tekken3Remake/work/jp_rev1/exe.bin')
    p.add_argument('span', nargs='+', help='USA file offset:size, e.g. 0x876CC:0x30')
    args = p.parse_args()
    usa, jp = args.usa_exe.read_bytes(), args.jp_exe.read_bytes()
    layout = Layout(LAYOUT)
    for span in args.span:
        at, size = (int(v, 0) for v in span.split(':'))
        va = BASE + at - 0x800
        jva = layout.to_jp(va, size)
        a, b = usa[at:at + size], jp[jva - BASE + 0x800:jva - BASE + 0x800 + size]
        words = [i for i in range(size // 2) if a[2 * i:2 * i + 2] != b[2 * i:2 * i + 2]]
        print(f'{span}: USA {va:#x} -> JP {jva:#x}: {"same" if not words else f"{len(words)} words differ, first {words[:8]}"}')


if __name__ == '__main__':
    main()
