#!/usr/bin/env python3
"""Print per-frame fighter facts from a fight_oracle_export.py case.

Fields are Rev.1 fighter-record offsets (fighter.md); the snapshots hold the
USA-layout records, which keep the same layout.
"""
import argparse
import struct
from pathlib import Path

USA_FIGHTERS, FIGHTER_SIZE = 0x800A9228, 0x188C
FIELDS = dict(slot=(0xA0, 'H'), pose=(0x58, 'h'), root_frame=(0x50, 'h'), transition=(0xA2, 'H'),
              ballistic=(0x92, 'h'), freeze=(0x76, 'h'), hold=(0x6A, 'h'), hp=(0x3F4, 'i'),
              x=(0x3C, 'i'), y=(0x40, 'i'), z=(0x44, 'i'), pending=(0x1A0, 'h'), got_hit=(0xCE, 'B'),
              guarded=(0xCF, 'B'), clean=(0xD0, 'B'))


def load(path):
    data = path.read_bytes()
    count = struct.unpack_from('<I', data, 8)[0]
    at, spans = 12, {}
    for _ in range(count):
        address, size = struct.unpack_from('<II', data, at)
        spans[address] = data[at + 8:at + 8 + size]
        at += 8 + size
    return spans


def read(spans, address, fmt):
    for base, data in spans.items():
        if base <= address < base + len(data):
            return struct.unpack_from('<' + fmt, data, address - base)[0]
    raise KeyError(hex(address))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('case', type=Path)
    p.add_argument('first', type=int)
    p.add_argument('last', type=int)
    p.add_argument('--fields', default='slot,pose,transition,ballistic,freeze,hp,pending,got_hit')
    p.add_argument('--offset', action='append', default=[], help='extra field: name=0xOFF:fmt')
    p.add_argument('--address', action='append', default=[], help='global RAM field: name=0xADDR:fmt')
    args = p.parse_args()
    fields = {k: FIELDS[k] for k in args.fields.split(',')}
    for extra in args.offset:
        name, spec = extra.split('=')
        off, fmt = spec.split(':')
        fields[name] = (int(off, 0), fmt)
    globals_ = []
    for extra in args.address:
        name, spec = extra.split('=')
        addr, fmt = spec.split(':')
        globals_.append((name, int(addr, 0), fmt))
    for frame in range(args.first, args.last + 1):
        path = args.case / f'frame-{frame}.ranges'
        if not path.exists():
            continue
        spans = load(path)
        cells = []
        for i in range(2):
            base = USA_FIGHTERS + i * FIGHTER_SIZE
            cells.append(' '.join(f'{k}={read(spans, base + o, f)}' for k, (o, f) in fields.items()))
        extra = ' '.join(f'{name}={read(spans, addr, fmt)}' for name, addr, fmt in globals_)
        print(frame, ' | '.join(cells), extra)


if __name__ == '__main__':
    main()
