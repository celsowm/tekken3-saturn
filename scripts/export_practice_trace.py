#!/usr/bin/env python3
"""Turn Tekken3Remake's flow trace of the practice mode into per-frame cases for tests/test_practice_golden.c.

Input: third_party/Tekken3Remake/work/traces/flow_practice.bin (python3 tools/research/flow_trace.py practice, with
work/venv). Output: one text line per frame in which practice's attack data ran, with the original's own values:

    prev(2 players x 16)  facts(2 fighters x 16)  next(2 players x 16)

prev and next are the practice state S's per-player block before and after the frame; the facts are the fighter
records' fields after it (see src/game/practice_data.h). The output is derived from the game and stays out of git
(build/ is ignored). The throw flag has no field in the records (it comes from the move row's flags), so the
facts carry the throw bit the original stored in S on the frame the opponent's move changed.
"""
import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TRACE = ROOT / 'third_party/Tekken3Remake/work/traces/flow_practice.bin'
S_BASE = 0x800B9180
PLAYER_DATA = 0x14
PAUSED = 0x8A
BUFFER_INPUT = 0x2000


def load(path):
    raw = path.read_bytes()
    assert raw[:4] == b'T3GF'
    _, head_len = struct.unpack_from('<II', raw, 4)
    header = json.loads(raw[12:12 + head_len])
    off = 12 + head_len
    body_len, packed_len = struct.unpack_from('<II', raw, off)
    body = zlib.decompress(raw[off + 8:off + 8 + packed_len])
    assert len(body) == body_len
    return header, body


def frames(header, body):
    """Yield (ranges dict of bytes, list of fighter record bytes) per frame."""
    pos = 0
    sizes = [(name, addr, size) for name, addr, size in header['ranges']]
    pointed = header['pointed']
    fsize = header['fighter_size']
    for _ in range(header['frames']):
        pos += 9
        ncalls = body[pos - 1]
        for _ in range(ncalls):
            _kind, argc = body[pos], body[pos + 1]
            pos += 2 + 4 * argc
        ranges = {}
        for name, addr, size in sizes:
            ranges[name] = (addr, body[pos:pos + size])
            pos += size
        for _name, _ptr, size in pointed:
            pos += size
        count = body[pos]
        pos += 1
        records = []
        for _ in range(count):
            records.append(body[pos:pos + fsize])
            pos += fsize
        yield ranges, records


def u8(b, o):
    return b[o]


def s8(b, o):
    return struct.unpack_from('<b', b, o)[0]


def u16(b, o):
    return struct.unpack_from('<H', b, o)[0]


def s16(b, o):
    return struct.unpack_from('<h', b, o)[0]


def s32(b, o):
    return struct.unpack_from('<i', b, o)[0]


def player_block(s, p):
    """S's per-player block, in the order of the cases' fields."""
    o = PLAYER_DATA + 0x20 * p
    return [s32(s, o), s32(s, o + 4), s32(s, o + 8), s16(s, o + 0xC), s16(s, o + 0xE), s16(s, o + 0x10),
            s16(s, o + 0x12), u8(s, o + 0x14), u8(s, o + 0x15), u8(s, o + 0x16), u8(s, o + 0x17),
            u8(s, o + 0x1A), u8(s, o + 0x1B), s8(s, o + 0x1C), u8(s, o + 0x1E), u8(s, o + 0x1F)]


def facts(rec, s_next, p, base):
    # +0x194 holds the address of the best hit slot, a pointer into the record's own two slots at +0x13C
    best = (struct.unpack_from('<I', rec, 0x194)[0] - base - 0x13C) // 0x2C
    slot = 0x13C + 0x2C * (best if best in (0, 1) else 0)
    changed = u8(rec, 0xDA)
    # the opponent's throw flag was stored in the other player's block when this fighter's move changed
    throw_bit = BUFFER_INPUT if u8(s_next, PLAYER_DATA + 0x20 * (1 - p) + 0x1E) and changed else 0
    return [u8(rec, 0xD0), u8(rec, 0xD3), u8(rec, 0xD4), u8(rec, 0xCE), u8(rec, 0x87), u8(rec, 0x8A), changed,
            u8(rec, 0xD2), s16(rec, 0xCC), s16(rec, 0x74), s16(rec, slot + 0x1E), u16(rec, slot + 0x1C),
            throw_bit, u16(rec, 0xA0), u8(rec, 0x16), s16(rec, 0x80)]


def guide_block(practice, s):
    """S's guide state: 11 scalars, the ring head of both players, and the 50 ring words."""
    heads = [s32(s, 0x1E8), s32(s, 0x1EC)]
    ring = [struct.unpack_from('<I', s, 0xB0 + 4 * i)[0] for i in range(50)]
    return [u8(s, 0x82), u8(s, 0xA3), u8(s, 0xA4), u8(s, 0xA2), u8(s, 0xA1), u8(s, 0xA8), s32(s, 0x178),
            struct.unpack_from('<I', s, 0x17C)[0], s32(s, 0x180), heads[0], heads[1]] + ring


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--trace', type=Path, default=TRACE)
    ap.add_argument('--out', type=Path, default=ROOT / 'build/practice-golden/cases.txt')
    args = ap.parse_args()
    header, body = load(args.trace)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    prev = None
    kept = 0
    keys = args.out.with_name('keys.txt')
    key_prev = None
    key_rows = 0
    marker_out = args.out.with_name('markers.txt').open('w')
    guide_out = args.out.with_name('guide.txt').open('w')
    guide_prev = None
    guide_rows = 0
    with args.out.open('w') as out, keys.open('w') as key_out:
        for ranges, records in frames(header, body):
            addr, practice = ranges['practice']
            s = practice[S_BASE - addr:]
            glob_addr, glob = ranges['practice_globals']
            active = len(records) == 2 and u8(glob, 0) != 0
            if not active:
                prev = None
                key_prev = None
                guide_prev = None
                continue
            # the guide / recorder (FUN_800B3808's state machine): S's guide block before and after the frame
            round_addr0, round_bytes0 = ranges['round']
            fight_now = s32(round_bytes0, 0x80097350 - round_addr0) == 1
            blk = guide_block(practice, s)
            if (guide_prev is not None and guide_prev[2] and fight_now and u8(s, PAUSED) == 0 and u8(guide_prev[1], PAUSED) == 0
                    and u8(guide_prev[1], 0x87) != 0 and u8(s, 0x87) != 0):
                cp = s8(s, 0x7B)
                if cp in (0, 1):
                    rec = records[cp]
                    me = records[s8(s, 0x84)] if s8(s, 0x84) in (0, 1) else records[0]
                    pad = struct.unpack_from('<I', practice, 8 + 4 * cp)[0] & 0xFFFF
                    inputs = [s8(s, 0x77), u8(s, 0x87), s8(s, 0x7C), cp, u16(s, 0), u16(s, 2), u16(s, 8), pad,
                              u8(rec, 0xC6), u16(rec, 0xA0), u8(rec, 0xD0),
                              1 if struct.unpack_from('<I', rec, 0x1A4)[0] else 0, 1 if s16(rec, 0x19A) == 4 else 0,
                              1 if u8(rec, 0x32) else 0, u8(rec, 0x16), u16(me, 0xA0)]
                    script_prev, script_next = u16(guide_prev[3][cp], 0x408), u16(rec, 0x408)
                    guide_out.write(' '.join(str(v) for v in inputs + guide_prev[0] + [script_prev] + blk + [script_next]) + '\n')
                    guide_rows += 1
            guide_prev = (blk, s, True, records)
            # the key ring (FUN_800B7B08): the combo player's ring before and after, with the frame's pad word
            cp = s8(s, 0x7B)
            round_addr, round_bytes = ranges['round']
            fighting = s32(round_bytes, 0x800 * 0 + (0x80097350 - round_addr)) == 1   # RoundFlow state: FIGHT
            if cp in (0, 1) and u8(s, PAUSED) == 0 and u8(s, 0x87) == 0 and fighting:
                head = s32(s, 0x1E8 + 4 * cp)
                pad = struct.unpack_from('<I', practice, 8 + 4 * cp)[0] & 0xFFFF
                entry = struct.unpack_from('<I', s, 0xB0 + 4 * head)[0] if 0 <= head < 50 else 0
                if key_prev is not None and key_prev[0] == cp:
                    side = u8(records[cp], 0xC6)
                    key_out.write(f'{key_prev[1]} {key_prev[2]} {pad} {side} {head} {entry}\n')
                    key_rows += 1
                key_prev = (cp, head, entry)
            else:
                key_prev = None
            nxt = [player_block(s, p) for p in range(2)]
            if prev is not None and u8(s, PAUSED) == 0 and u8(prev[1], PAUSED) == 0:
                pm, nm = prev[1], s
                marker_out.write(' '.join(str(v) for v in [u8(pm, 0x74)] + [s16(pm, 0x54 + 8 * i + o) for i in range(4) for o in (0, 2)]
                                 + [1 if u8(records[1], 0xD0) else 0, u16(records[0], 0x64), u8(nm, 0x74)]
                                 + [s16(nm, 0x54 + 8 * i + o) for i in range(4) for o in (0, 2)]) + '\n')
                row = [v for p in range(2) for v in prev[0][p]]
                row += [v for p in range(2) for v in facts(records[p], s, p, header['fighters'][p])]
                row += [v for p in range(2) for v in nxt[p]]
                out.write(' '.join(str(v) for v in row) + '\n')
                kept += 1
            prev = (nxt, s)
    print(f'{kept} frames -> {args.out}, {key_rows} key frames -> {keys}, {guide_rows} guide frames')
    return 0 if kept else 1


if __name__ == '__main__':
    sys.exit(main())
