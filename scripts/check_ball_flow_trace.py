#!/usr/bin/env python3
"""Replay src/game/ball_rules.c over a recorded Tekken Ball game (the flow trace of the pinned Tekken3Remake,
`tools/research/flow_trace.py ball` or `ball_versus`) and compare the ball with the original's frame by frame.

Each fight frame k is stepped from the ball and engine words recorded after frame k-1, with the fighters' facts
(positions, attack segments, hurt cylinders, move level and damage) as frame k left them, and its result is
compared with the ball recorded after frame k. The fields the ball writes into a fighter in a hit (locked, f88, fd1, f23) are taken from frame k-1: the hit that sets
them is this frame's, and its touch test runs before the write (without this, a serve that a fighter hits is missed).
The fighters' move records are not in the trace: every move is taken
as an ordinary one (no projectile segments) and the stand-in's move byte is 0. `--s7` is the caller's register the
original reuses as the hit mask on frames that skip the hit tests (game-bugs.md #44). Run with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/check_ball_flow_trace.py \
        --trace third_party/Tekken3Remake/work/traces/flow_ball.bin
"""
import argparse
import ctypes
import json
import struct
import subprocess
import sys
import tempfile
import zlib
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import export_ball_cases as eb   # noqa: E402
from fight_sim import Ram        # noqa: E402

FIGHT = 8
ENGINE_WORDS = (eb.bs.LOB_BASE, 0x80097350, eb.bs.NO_SCORE, eb.bs.NO_WINNER_MARK, eb.bs.ROUND_CLOCK)
STATE_RANGE = 'state'
GAUGES = 0x800B68E8
# the fighter fields t3_ball_update writes in a hit (the hitter's +0x85, +0x88, +0xD1 and +0x23 words), by address offset
BALL_WRITTEN = {'locked': 0x85, 'f88': 0x88, 'fd1': 0xD1, 'f23': 0x23}


def load_trace(path):
    d = path.read_bytes()
    _, hl = struct.unpack_from('<II', d, 4)
    header = json.loads(d[12:12 + hl])
    at = 12 + hl
    _, zl = struct.unpack_from('<II', d, at)
    return header, zlib.decompress(d[at + 8:at + 8 + zl])


def frames(header, body):
    at = 0
    for _ in range(header['frames']):
        pads = struct.unpack_from('<HHHHB', body, at)
        at += 9
        for _ in range(pads[4]):
            at += 2 + 4 * body[at + 1]
        ranges = {}
        for name, addr, size in header['ranges']:
            ranges[name] = (addr, body[at:at + size])
            at += size
        pointed = {}
        for name, addr, size in header['pointed']:
            pointed[name] = body[at:at + size]
            at += size
        count = body[at]
        at += 1
        records = []
        for _ in range(count):
            records.append(body[at:at + header['fighter_size']])
            at += header['fighter_size']
        yield pads, ranges, pointed, records


def build_ram(header, ranges, pointed, records, fighters_only=None):
    ram = Ram(bytes(0x200000))
    for name, (addr, data) in ranges.items():
        off = ram._off(addr)
        ram.data[off:off + len(data)] = data
    ball = pointed['ball']
    off = ram._off(eb.BALL)
    ram.data[off:off + len(ball)] = ball
    ram.put(eb.bs.BALL_PTR, 'I', eb.BALL)
    for base, rec in zip(header['fighters'], records):
        off = ram._off(base)
        ram.data[off:off + len(rec)] = rec
    return ram


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--s7', type=int, default=0)
    parser.add_argument('--max-frames', type=int, default=0)
    parser.add_argument('--show', type=int, default=6)
    args = parser.parse_args()
    lib_path = Path(tempfile.mkdtemp()) / 'ball_rules.so'
    subprocess.run(['cc', '-O2', '-shared', '-fPIC', '-I', str(ROOT / 'src/game'), '-I', str(ROOT.parent / 'lib-saturn/include'),
                    '-o', str(lib_path), str(ROOT / 'tests/ball_rules_shim.c'), str(ROOT / 'src/game/ball_rules.c'),
                    str(ROOT / 'src/game/psx_exact_contact.c'), str(ROOT / 'src/game/psx_collision.c'),
                    str(ROOT / 'src/game/psx_pose.c'), str(ROOT / 'src/game/ball_hud.c')], check=True)
    lib = ctypes.CDLL(str(lib_path))
    lib.ball_gauge.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_int32, ctypes.c_int32]
    exe = (ROOT / 'third_party/Tekken3Remake/work/jp_rev1/exe.bin').read_bytes()[0x800:]
    trig = exe[eb.bs.pj.SIN_TABLE - eb.EXE_BASE:eb.bs.pj.SIN_TABLE - eb.EXE_BASE + eb.TRIG_BYTES]
    atan = exe[eb.ATAN_TABLE - eb.EXE_BASE:eb.ATAN_TABLE - eb.EXE_BASE + eb.ATAN_BYTES]
    n = len(eb.FIELDS)
    names = [f.name for f in eb.FIELDS]
    ball_n = len(eb.ball_fields())
    watched = set(range(ball_n)) | {names.index(x) for x in ('idle', 'touch_sound_wait', 'point', 'charged_hit')}
    watched |= {names.index(f'{a}health') for a in ('', )} if False else set()
    header, body = load_trace(args.trace)
    previous = None
    checked = same = skipped = gauges = gauges_same = shown_gauge = 0
    bad = Counter()
    shown = 0
    for k, (pads, ranges, pointed, records) in enumerate(frames(header, body)):
        if args.max_frames and k > args.max_frames:
            break
        try:
            state_addr, state = ranges['state']
            in_fight = struct.unpack_from('<I', state, 12)[0] == FIGHT
        except Exception:
            in_fight = True
        cur = build_ram(header, ranges, pointed, records)
        if previous is not None and in_fight and cur.u32(eb.bs.BALL_PTR) == eb.BALL:
            pre = build_ram(header, *previous)
            for i in range(2):                       # the fighters as frame k left them
                off = pre._off(eb.FIGHTERS[i])
                pre.data[off:off + header['fighter_size']] = records[i]
                if i < len(previous[2]):             # the ball's own writes are not yet made when its touch tests run
                    for at in BALL_WRITTEN.values():
                        pre.data[off + at:off + at + 4] = previous[2][i][at:at + 4]
            for a in ENGINE_WORDS:                   # the engine's words as frame k has them when the ball runs
                pre.put(a, 'I', cur.u32(a))
            # the replay of a deciding point plays the ball back from the ring; a new round or the select screen restarts it
            frozen = (pre.u32(eb.bs.FROZEN) or pre.u32(eb.bs.FROZEN_FIGHT) or pre.u32(eb.bs.REPLAY_PLAYBACK) or
                      cur.u32(eb.bs.REPLAY_PLAYBACK) or pre.u8(eb.BALL + 0x146) == 10 or cur.u8(eb.BALL + 0x146) == 10 or
                      (cur.s16(eb.BALL) == 0 and cur.u8(eb.BALL + 0x147) in (40, 100)) or pre.s16(eb.BALL) == 0 and
                      not any(pre.s32(eb.BALL + 0x68 + 4 * i) for i in range(3)) and pre.u8(eb.BALL + 0x147) == 0)
            skipped += bool(frozen)
            if pre.s32(eb.bs.TOUCH_SOUND_WAIT) > 0:
                pre.put(eb.bs.TOUCH_SOUND_WAIT, 'I', pre.s32(eb.bs.TOUCH_SOUND_WAIT) - 1)
            before = eb.vector(pre, [0, 0])
            want = eb.vector(cur, [0, 0])
            if not frozen and pre.s16(eb.BALL) != cur.s16(eb.BALL) or not frozen and before[:ball_n] != want[:ball_n]:
                state_v = (ctypes.c_int32 * n)(*[v if -2**31 <= v < 2**31 else v - 2**32 for v in before])
                ev = (ctypes.c_int32 * 100)()
                ok = lib.ball_run(state_v, trig, len(trig), atan, len(atan), args.s7, ev)
                got = list(state_v)
                diffs = [i for i in watched if got[i] != want[i]]
                checked += 1
                if not diffs:
                    same += 1
                else:
                    for i in diffs:
                        bad[names[i]] += 1
                    if shown < args.show:
                        shown += 1
                        print(f'frame {k}: ' + ', '.join(f'{names[i]} {got[i]} want {want[i]}' for i in sorted(diffs)[:6]))
        if previous is not None and in_fight and cur.u32(eb.bs.BALL_PTR) == eb.BALL:
            pre = build_ram(header, *previous)
            for g in range(2):
                r = GAUGES + 0x10 * g
                state = (ctypes.c_int32 * 4)(pre.u32(r) - (1 << 32 if pre.u32(r) >= 1 << 31 else 0),
                                             pre.s16(r + 4), pre.s16(r + 6), pre.s16(r + 8))
                lib.ball_gauge(state, cur.s16(eb.BALL + 0x13C + 2 * g), 100)
                want = (cur.u32(r), cur.s16(r + 4), cur.s16(r + 6), cur.s16(r + 8))
                got = (state[0] & 0xFFFFFFFF, state[1], state[2], state[3])
                gauges += 1
                if got == want:
                    gauges_same += 1
                elif shown_gauge < 3:
                    shown_gauge += 1
                    print(f'frame {k} gauge {g}: {got} want {want}')
        previous = (ranges, pointed, records)
    print(f'{skipped} frames skipped (frozen, replay playback, restart)')
    print(f'{checked} ball frames checked, {same} equal ({100.0 * same / max(checked, 1):.1f}%)')
    print(f'{gauges} gauge frames checked, {gauges_same} equal ({100.0 * gauges_same / max(gauges, 1):.1f}%)')
    if bad:
        print('fields that differ:', dict(bad.most_common(12)))


if __name__ == '__main__':
    main()
