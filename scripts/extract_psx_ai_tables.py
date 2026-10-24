#!/usr/bin/env python3
"""Extract the private executable tables of the CPU opponent (src/game/psx_ai.c).

The AI reads its tables at Japan Rev.1 addresses. This takes them from the USA
executable through the fingerprinted release layout and rewrites the pointers
inside them (script, sequence and setup pointers) to Rev.1 addresses, so the
blob holds what the original game has in memory. With --rev1 the result is
compared byte for byte with the Rev.1 executable.

Output (git-ignored): "T3AI", u32 window count, then per window u32 Rev.1
address, u32 size, u32 data offset; then the data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
BASE, HEADER = 0x80010000, 0x800
# (Rev.1 address, size): the input sequences and their bodies, the scripts,
# guard and bank-setup tables, AI parameters and pads, the sequence pointer
# tables, and the reach and band tables. The common branch rows (0x80017C78)
# come from psx_common_branches.bin.
WINDOWS = ((0x8001006C, 0x6D0), (0x80022E90, 0x270), (0x80023288, 0xE78),
           (0x800958F4, 0x1A0), (0x80098610, 0x300))
MAGIC = b'T3AI'


class TableError(Exception):
    pass


def to_usa(segments, address):
    offset = address - BASE
    for start, length, source in segments:
        if start <= offset < start + length:
            return source + offset - start + BASE, start + length - offset
    raise TableError(f'{address:#x} is not in the USA layout')


def to_rev1(segments, address):
    offset = address - BASE
    found = [start + offset - source for start, length, source in segments
             if source <= offset < source + length]
    return found[0] + BASE if len(found) == 1 else None


def window_bytes(raw, segments, address, size):
    """The window's bytes, segment by segment."""
    out = bytearray()
    while len(out) < size:
        usa, left = to_usa(segments, address + len(out))
        take = min(left, size - len(out))
        at = usa - BASE + HEADER
        out += raw[at:at + take]
    return out


def inside(address):
    return any(a <= address < a + n for a, n in WINDOWS) or 0x80017C78 <= address < 0x80017C78 + 672 * 12


def extract(exe, layout_file, rev1=None):
    raw = exe.read_bytes()
    layout = json.loads(layout_file.read_text())['blocks']['exe']
    if hashlib.sha256(raw[HEADER:]).hexdigest() != layout['sha256']:
        raise TableError('USA executable body does not match release layout')
    segments = layout['segments']
    end = BASE + len(raw) - HEADER
    blobs, pointers, outside = [], 0, []
    for address, size in WINDOWS:
        data = window_bytes(raw, segments, address, size)
        for at in range(0, size - 3, 4):
            word = struct.unpack_from('<I', data, at)[0]
            if not BASE <= word < end:
                continue
            moved = to_rev1(segments, word)
            if moved is None:
                raise TableError(f'pointer {word:#x} at {address + at:#x} is not in the Rev.1 layout')
            struct.pack_into('<I', data, at, moved)
            pointers += 1
            if not inside(moved):
                outside.append((address + at, moved))
        if rev1:
            ref = rev1[address - BASE + HEADER:address - BASE + HEADER + size]
            if ref != data:
                first = next(i for i in range(size) if ref[i] != data[i])
                raise TableError(f'window {address:#x} differs from Rev.1 at {address + first:#x}')
        blobs.append(bytes(data))
    head = MAGIC + struct.pack('<I', len(WINDOWS))
    offset = len(head) + 12 * len(WINDOWS)
    table, body = b'', b''
    for (address, size), data in zip(WINDOWS, blobs):
        table += struct.pack('<III', address, size, offset + len(body))
        body += data
    return head + table + body, pointers, outside


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    p.add_argument('--layout', type=Path,
                   default=ROOT / 'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json')
    p.add_argument('--rev1', type=Path, help='Japan Rev.1 executable to compare with')
    p.add_argument('--output', type=Path, default=ROOT / 'assets/imported/tekken3-usa/pose-tables/psx_ai.bin')
    a = p.parse_args()
    blob, pointers, outside = extract(a.exe, a.layout, a.rev1.read_bytes() if a.rev1 else None)
    a.output.write_bytes(blob)
    # Code addresses (hooks) and tables the AI does not read land outside.
    for at, target in outside:
        print(f'  pointer at {at:#x} -> {target:#x} is outside the windows')
    print(f'{a.output}: {len(blob)} bytes, {pointers} pointers moved to Rev.1'
          + (', identical to Rev.1' if a.rev1 else ''))


if __name__ == '__main__':
    raise SystemExit(main())
