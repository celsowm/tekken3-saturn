#!/usr/bin/env python3
"""Group a Ymir debug profile's per-PC cycles by object file.

Reads the debug.pc.csv that `saturn_probe.py --debug-profile` writes and the
linker map of the same build (`profile.map` in the probe output), assigns each
executed PC to the input section that contains it, and prints cycles per
traced frame for each object file, per CPU. No retail data needed.

    profile_by_object.py PROFILE/debug.pc.csv PROFILE/profile.map --frames 20
"""
import argparse
import bisect
import csv
import re
from collections import defaultdict
from pathlib import Path

SECTION = re.compile(r'^ (\.\S+)\s*$')
PLACED = re.compile(r'^\s+(?:(\.\S+)\s+)?0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s+(\S.*)$')


def sections(map_path):
    """(start, end, object) for every non-empty code input section."""
    out = []
    pending = None
    for line in Path(map_path).read_text(errors='replace').splitlines():
        m = SECTION.match(line)
        if m:
            pending = m.group(1)
            continue
        m = PLACED.match(line)
        if m:
            name = m.group(1) or pending
            pending = None
            start, size = int(m.group(2), 16), int(m.group(3), 16)
            if name and name.startswith('.text') and size and start:
                obj = m.group(4).strip()
                obj = re.sub(r'^.*/', '', obj)
                out.append((start, start + size, obj))
        else:
            pending = None
    out.sort()
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('csv')
    p.add_argument('map')
    p.add_argument('--frames', type=int, default=1, help='traced frames')
    p.add_argument('--top', type=int, default=30)
    a = p.parse_args()
    ranges = sections(a.map)
    starts = [r[0] for r in ranges]
    totals = defaultdict(lambda: defaultdict(int))
    with open(a.csv) as f:
        for row in csv.DictReader(f):
            pc, cycles = int(row['pc']), int(row['execution_cycles'])
            i = bisect.bisect_right(starts, pc) - 1
            obj = ranges[i][2] if i >= 0 and pc < ranges[i][1] else '(unmapped)'
            totals[row['cpu']][obj] += cycles
    for cpu, objs in totals.items():
        total = sum(objs.values())
        print(f'== {cpu}: {total // a.frames} cycles per frame')
        for obj, c in sorted(objs.items(), key=lambda kv: -kv[1])[:a.top]:
            print(f'  {c // a.frames:8d} {100 * c / total:5.1f}%  {obj}')


if __name__ == '__main__':
    main()
