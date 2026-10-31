#!/usr/bin/env python3
"""List what occupies a RAM range of a GNU ld map, largest first, by object file."""
import argparse
import collections
from pathlib import Path
import re

ROW = re.compile(r'^ (\S+)?\s*(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+\.(?:o|obj|a\S*))\s*$')


def sizes(text, low, high):
    totals = collections.Counter()
    pending = None
    for line in text.splitlines():
        if line.startswith(' .') and len(line.split()) == 1:
            pending = line.strip()
            continue
        match = ROW.match(pending and ' %s %s' % (pending, line.strip()) or line)
        pending = None
        if match and low <= int(match[2], 16) < high:
            kind = (match[1] or '').split('.')[1:2] or ['?']
            totals[(Path(match[4]).name, kind[0])] += int(match[3], 16)
    return totals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('map', type=Path)
    parser.add_argument('--low', type=lambda v: int(v, 0), default=0x06004000)
    parser.add_argument('--high', type=lambda v: int(v, 0), default=0x06100000)
    parser.add_argument('--top', type=int, default=30)
    args = parser.parse_args()
    for (name, kind), size in sizes(args.map.read_text(), args.low, args.high).most_common(args.top):
        print('%8d %-8s %s' % (size, kind, name))


if __name__ == '__main__':
    main()
