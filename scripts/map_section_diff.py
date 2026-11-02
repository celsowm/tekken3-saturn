#!/usr/bin/env python3
"""Input sections (with size) that a GNU ld map has and a baseline map lacks, in the RAM range, largest first."""
import argparse
import re
from pathlib import Path

ROW = re.compile(r'^ (\S+)\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+(\S+)$')


def sections(path, low, high):
    found = {}
    pending = None
    for line in path.read_text().splitlines():
        if line.startswith(' .') and len(line.split()) == 1:
            pending = line.strip()
            continue
        match = ROW.match(' %s %s' % (pending, line.strip()) if pending else line)
        pending = None
        if match and low <= int(match[2], 16) < high and int(match[3], 16):
            key = (match[1], Path(match[4]).name if '(' not in match[4] else match[4].split('(')[1].rstrip(')'))
            found[key] = found.get(key, 0) + int(match[3], 16)
    return found


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('map', type=Path)
    parser.add_argument('--low', type=lambda v: int(v, 0), default=0x06004000)
    parser.add_argument('--high', type=lambda v: int(v, 0), default=0x06200000)
    args = parser.parse_args()
    base = sections(args.baseline, args.low, args.high)
    new = sections(args.map, args.low, args.high)
    total = 0
    for key, size in sorted(((k, v - base.get(k, 0)) for k, v in new.items() if v != base.get(k, 0)),
                            key=lambda row: -row[1]):
        total += size
        print('%7d %-60s %s' % (size, key[0][:60], key[1]))
    print('%7d total' % total)


if __name__ == '__main__':
    main()
