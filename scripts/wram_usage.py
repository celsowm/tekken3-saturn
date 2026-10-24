#!/usr/bin/env python3
"""List the largest input sections a Saturn link map places in a memory range.

Reads the GNU ld map (`tekken3_saturn.elf.map`), which exists even when the
link fails on the Work RAM High stack assertion, so it shows what to move.
Default range: Work RAM High data and bss (after .text).
"""
import argparse
import re
from pathlib import Path

LINE = re.compile(r'^ (\.\S+|COMMON)\s*$|^ (\.\S+|COMMON)?\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)')


def sections(text):
    """Yield (name, address, size, object) for every input section line."""
    pending = None
    for line in text.splitlines():
        m = LINE.match(line)
        if not m:
            pending = None
            continue
        if m.group(1):
            pending = m.group(1)
            continue
        name = m.group(2) or pending
        pending = None
        if name:
            yield name, int(m.group(3), 16), int(m.group(4), 16), m.group(5)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('map', type=Path, nargs='?', default=Path('build/t30-law/tekken3_saturn.elf.map'))
    p.add_argument('--start', type=lambda v: int(v, 0), default=0x06004000)
    p.add_argument('--end', type=lambda v: int(v, 0), default=0x06100000)
    p.add_argument('--kind', default='.bss,.data,COMMON', help='comma-separated section name prefixes')
    p.add_argument('--top', type=int, default=30)
    args = p.parse_args()
    kinds = tuple(args.kind.split(','))
    rows = [r for r in sections(args.map.read_text(errors='replace'))
            if args.start <= r[1] < args.end and r[2] and r[0].startswith(kinds)]
    rows.sort(key=lambda r: -r[2])
    for name, address, size, obj in rows[:args.top]:
        print(f'{size:8d} {address:#010x} {name} {Path(obj).name}')
    print(f'{sum(r[2] for r in rows):8d} total in {len(rows)} sections')


if __name__ == '__main__':
    main()
