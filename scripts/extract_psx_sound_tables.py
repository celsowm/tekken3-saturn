#!/usr/bin/env python3
"""Private fighter sound tables, normalized to offsets; no game data in Git.

Rules/addresses: pinned Tekken3Remake tools/remake_import/tables.py and
docs/research/code/sound.md (PolyForm Noncommercial, see NOTICE.md).
The USA executable is fingerprinted before following any pointer. --rev1
compares the complete normalized result with Japan Rev.1.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from extract_psx_ai_tables import to_usa

ROOT = Path(__file__).resolve().parents[1]
BASE, HEADER = 0x80010000, 0x800
VOICES, SCRIPTS = 23, 470


def normalize(raw, segments=None):
    def read(address, size):
        if segments is not None:
            address, left = to_usa(segments, address)
            if size > left:
                raise ValueError('sound table crosses release mapping segment')
        at = address - BASE + HEADER
        if not HEADER <= at <= len(raw) - size:
            raise ValueError('sound table outside executable')
        return raw[at:at + size]

    # Pointer targets in a USA record are already USA addresses.
    def pointed(address, size):
        at = address - BASE + HEADER
        if not HEADER <= at <= len(raw) - size:
            raise ValueError('sound pointer outside executable')
        return raw[at:at + size]

    head = 24
    voice_at, script_at = head, head + 24 * VOICES
    aux_at = script_at + 4 * SCRIPTS
    out = bytearray(aux_at + 20)
    struct.pack_into('<4sIHHIII', out, 0, b'T3SF', 1, VOICES, SCRIPTS,
                     voice_at, script_at, aux_at)
    for index in range(VOICES):
        row = read(0x8001AD54 + 24 * index, 24)
        pointers = struct.unpack_from('<3I', row)
        counts = struct.unpack('<4h', pointed(pointers[0], 8))
        shouts = struct.unpack('<h', pointed(pointers[1], 2))[0]
        damage = struct.unpack('<2h', pointed(pointers[2], 4))
        if any(n < 0 or n > 256 for n in (*counts, shouts, *damage)):
            raise ValueError('invalid voice list length')
        sizes = (8 + 2 * sum(counts), 2 + 2 * max(shouts, 1), 4 + 2 * sum(damage))
        offsets = []
        for pointer, size in zip(pointers, sizes):
            offsets.append(len(out))
            out += pointed(pointer, size)
        out[voice_at + 24 * index:voice_at + 24 * index + 24] = struct.pack('<3I', *offsets) + row[12:]
    for index in range(SCRIPTS):
        pointer = struct.unpack('<I', read(0x8009761C + 4 * index, 4))[0]
        struct.pack_into('<I', out, script_at + 4 * index, len(out))
        for n in range(4096):
            word = pointed(pointer + 4 * n, 4)
            out += word
            if struct.unpack('<I', word)[0] & 0xFFF == 0:
                break
        else:
            raise ValueError('unterminated sound script')
    out[aux_at:aux_at + 20] = (read(0x8001A6F0, 10) + read(0x8001A6FC, 4)
                                  + read(0x8001AD4C, 6))
    return bytes(out)


def select_scripts(blob, wanted):
    """Keep voice sets and the scripts reachable from this build's move banks.

    The 470-entry directory retains original ids; absent scripts have an
    invalid offset so accidental use fails instead of playing another id.
    No sound script starts another script (types 0..3 only).
    """
    if blob[:8] != b'T3SF\x01\0\0\0':
        raise ValueError('unsupported sound table format')
    voices, scripts, aux = struct.unpack_from('<3I', blob, 12)
    first = aux + 20
    out = bytearray(blob[:first])
    for index in range(VOICES):
        row = voices + index*24
        pointers = struct.unpack_from('<3I', blob, row)
        counts = struct.unpack_from('<4h', blob, pointers[0])
        shouts = struct.unpack_from('<h', blob, pointers[1])[0]
        damage = struct.unpack_from('<2h', blob, pointers[2])
        for k, size in enumerate((8 + 2*sum(counts), 2 + 2*max(shouts,1), 4 + 2*sum(damage))):
            struct.pack_into('<I', out, row+4*k, len(out))
            out += blob[pointers[k]:pointers[k]+size]
    for index in range(SCRIPTS):
        if index not in wanted:
            struct.pack_into('<I', out, scripts+4*index, 0xFFFFFFFF)
            continue
        start = struct.unpack_from('<I', blob, scripts+4*index)[0]
        end = start
        while struct.unpack_from('<I', blob, end)[0] & 0xFFF:
            end += 4
        struct.pack_into('<I', out, scripts+4*index, len(out))
        out += blob[start:end+4]
    return bytes(out)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, default=ROOT / 'assets/imported/tekken3-usa/SLUS_004.02')
    p.add_argument('--layout', type=Path, default=ROOT / 'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json')
    p.add_argument('--rev1', type=Path)
    p.add_argument('--output', type=Path, default=ROOT / 'assets/imported/tekken3-usa/pose-tables/psx_sounds.bin')
    a = p.parse_args()
    raw = a.exe.read_bytes()
    layout = json.loads(a.layout.read_text())['blocks']['exe']
    if hashlib.sha256(raw[HEADER:]).hexdigest() != layout['sha256']:
        raise ValueError('USA executable body does not match release layout')
    blob = normalize(raw, layout['segments'])
    if a.rev1 and blob != normalize(a.rev1.read_bytes()):
        raise ValueError('normalized sound tables differ from Japan Rev.1')
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_bytes(blob)
    print(json.dumps(dict(bytes=len(blob), sha256=hashlib.sha256(blob).hexdigest(),
                         rev1_identical=bool(a.rev1), voice_sets=VOICES, scripts=SCRIPTS)))


if __name__ == '__main__':
    main()
