#!/usr/bin/env python3
"""Replay src/game/psx_force_level.c over a recorded Tekken Force game (the flow trace of the pinned
Tekken3Remake, `tools/research/flow_trace.py force`) and compare it with the original's state frame by frame.

Each Force fight frame k is stepped from the state recorded after frame k-1 (the player, the camera, the
enemies' health and every other input the level runner reads are taken from the trace) and its result is
compared with the state recorded after frame k, field by field. The fight timer is not in the trace's
ranges, so the fields that depend on it are not compared. Run with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/check_force_flow_trace.py \
        --trace third_party/Tekken3Remake/work/traces/flow_force.bin --levels assets/imported/force-packs \
        --tables build/force-cases/TABLES.BIN
"""
import argparse
import ctypes
import struct
import subprocess
import sys
import tempfile
import zlib
import json
import math
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import export_force_level_cases as ex   # noqa: E402
from fight_sim import Ram               # noqa: E402

FORCE_FIGHTERS = (0x800AAF7C, 0x800AC808)
# popup positions come from the projection (not in C); f04, f92 and fb8 are written by the fight as well
UNTRACKED = {'timer', 'fight_start_timer', 'clear_time', 'bonus_time', 'clear_bonus', 'score', 'hiscore_run'}
UNTRACKED_FIELDS = {'popup_x', 'popup_y', 'f04', 'f92', 'fb8'}
# inputs the fight changes after the level runner has read them: from before the frame (the freeze flag)
EARLY = {'frozen'}
# What the level runner writes (the rest of the vector is input from the fight, or drawing).
OWNED = {'level', 'state', 'clock', 'drawn', 'tally', 'tally_done', 'keys', 'key_earned', 'final_challenge',
         'wait_defeated', 'wait_count', 'pattern_on', 'pattern_stop_x', 'script_ptr', 'scroll_mode',
         'scroll_param', 'sub_mode', 'flag_70e8', 'kills', 'mode_flags', 'bound0', 'bound1', 'bound2', 'bound3'}



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
        pads = struct.unpack_from('<HHHH', body, at)
        at += 8
        calls = body[at]
        at += 1
        for _ in range(calls):
            at += 2 + 4 * body[at + 1]
        ranges = {}
        for name, addr, size in header['ranges']:
            ranges[(name, addr)] = body[at:at + size]
            at += size
        for _, _, size in header['pointed']:
            at += size
        count = body[at]
        at += 1
        records = []
        for _ in range(count):
            records.append(body[at:at + header['fighter_size']])
            at += header['fighter_size']
        yield pads, ranges, records


def build_ram(header, ranges, records):
    ram = Ram(bytes(0x200000))
    for (name, addr), data in ranges.items():
        off = ram._off(addr)
        ram.data[off:off + len(data)] = data
    for base, rec in zip(header['fighters'], records):
        off = ram._off(base)
        ram.data[off:off + len(rec)] = rec
    return ram


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--levels', type=Path, required=True)
    parser.add_argument('--tables', type=Path, required=True)
    parser.add_argument('--max-frames', type=int, default=0)
    parser.add_argument('--pairs', action='store_true', help='also check the pair-distance table (needs a trace with the pair_table range)')
    parser.add_argument('--trig', type=Path, help='TRIG.BIN: also check the side-scrolling camera')
    args = parser.parse_args()
    lib_path = Path(tempfile.mkdtemp()) / 'force_level.so'
    subprocess.run(['cc', '-O2', '-shared', '-fPIC', '-I', str(ROOT / 'src/game'), '-o', str(lib_path),
                    str(ROOT / 'src/game/psx_force_level.c'), str(ROOT / 'tests/force_level_shim.c'), str(ROOT / 'src/game/psx_force_camera.c'),
                    str(ROOT / 'tests/force_camera_shim.c')], check=True)
    lib = ctypes.CDLL(str(lib_path))
    lib.force_step.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.POINTER(ctypes.c_int32), ctypes.c_char_p,
                               ctypes.c_char_p, ctypes.POINTER(ctypes.c_int32)]
    tables = args.tables.read_bytes()
    scripts = []
    for n in range(1, 5):
        d = (args.levels / f'LEV{n}.BIN').read_bytes()
        h = struct.unpack_from('>13I', d)
        scripts.append(d[h[11]:h[11] + h[12]])
    header, body = load_trace(args.trace)
    ex.FIGHTER = FORCE_FIGHTERS
    ex.PLAYER_FIGHTER = header['fighters'][0]
    names = [n for n, _, _ in ex.GLOBALS] + [f'slot{i}.{n}' for i in range(2) for n, _, _ in ex.SLOT] + \
        [f'fighter{i}.{n}' for i in range(2) for n, _, _ in ex.FIGHTER_FIELDS] + \
        [f'item{i}.{n}' for i in range(2) for n, _, _ in ex.ITEM] + [f'player.{n}' for n, _, _ in ex.PLAYER_FIELDS]
    trig = args.trig.read_bytes() if args.trig else None
    lib.force_camera_step.argtypes = [ctypes.POINTER(ctypes.c_int32), ctypes.c_char_p, ctypes.c_int32, ctypes.c_int32, ctypes.c_int32]
    cam_bad, cam_total, cam_prev, cam_player = 0, 0, None, None
    pair_bad = pair_total = 0
    pair_roots = None
    previous, script_base = None, None
    bad, total = Counter(), 0
    shown = 0
    for k, (pads, ranges, records) in enumerate(frames(header, body)):
        if args.max_frames and k >= args.max_frames:
            break
        if len(records) != 3:
            previous = None
            continue
        ram = build_ram(header, ranges, records)
        if ram.s32(0x800AFF50) != 8:
            previous = None
            continue
        if script_base is None:
            if not ram.u32(ex.fs.SCRIPT_PTR):
                previous = None
                continue
            script_base = ram.u32(ex.fs.SCRIPT_PTR)
        ex.SCRIPT = script_base
        vec = ex.read_vector(ram, ram.u8(ex.PLAYER_FIGHTER + 0x1E))
        if previous is not None and vec[names.index('clock')]==previous[names.index('clock')]+1:
            level = previous[names.index('level')]
            if 0 <= level < 4:
                def owned(n):
                    if n in UNTRACKED or n.split('.')[-1] in UNTRACKED_FIELDS:
                        return False
                    if n in OWNED:
                        return True
                    return n.startswith(('slot', 'item')) or (n.startswith('fighter') and n.split('.')[-1] in
                                                              ('fc2', 'fc3', 'health_max', 'key', 'f92', 'fb8', 'f04'))
                # the runner's own state from before the frame; what the fight and the camera wrote earlier in
                # the same frame (the camera, the fighters' health and positions) from after it
                mixed = [previous[i] if owned(names[i]) or names[i].split('.')[-1] in EARLY else vec[i]
                         for i in range(ex.VECTOR_WORDS)]
                inp = (ctypes.c_int32 * ex.VECTOR_WORDS)(*mixed)
                out = (ctypes.c_int32 * ex.VECTOR_WORDS)()
                events = (ctypes.c_int32 * (4 * ex.EVENTS + 1))()
                lib.force_step(inp, out, tables, scripts[level], events)
                total += 1
                diffs = [names[i] for i in range(ex.VECTOR_WORDS) if out[i] != vec[i] and owned(names[i])]
                for n in diffs:
                    bad[n] += 1
                if diffs and shown < 8:
                    shown += 1
                    print(f'frame {k}: ' + ', '.join(f'{n} {out[names.index(n)]}/{vec[names.index(n)]}' for n in diffs[:6]))
        if trig and ram.u32(0x800B7098):
            # the camera runs once a frame: from the record after the previous frame, with the player's position
            # after the previous one (the camera runs before the fighters move)
            rec = struct.unpack_from('<I13i', bytes(ram.data[ram._off(0x800B7098):ram._off(0x800B7098) + 56]))
            player_now = (ram.s32(ram.u32(0x800B7098) + 0xF68), ram.s32(ram.u32(0x800B7098) + 0xF70))
            if cam_prev is not None and cam_player is not None and ram.s32(0x80097350) == 1:
                arr = (ctypes.c_int32 * 14)(*[v - (1 << 32) if i == 0 and v >= 1 << 31 else v for i, v in enumerate(cam_prev)])
                lib.force_camera_step(arr, trig, ram.s32(0x800A8A90), cam_player[0], cam_player[1])
                got = [v & 0xFFFFFFFF if i == 0 else v for i, v in enumerate(arr)]
                cam_total += 1
                # the scroll mode (field 7) and the bounds are written by the level runner in the same frame
                keep = [i for i in range(1, 14) if i not in (7, 10, 11, 12, 13)]
                if [got[i] for i in keep] != [rec[i] for i in keep]:
                    cam_bad += 1
                    if cam_bad <= 5:
                        print(f'camera frame {k}:', [(i, got[i], rec[i]) for i in keep if got[i] != rec[i]])
            cam_prev, cam_player = rec, player_now
        if args.pairs and ram.s32(0x80097350) != 1:
            pair_roots = None
        if args.pairs and ram.s32(0x80097350) == 1:
            # PairwiseDistances (the match core's pair_distance for three fighters): the table after a frame holds
            # the distances of the fighters' roots as they were when the frame began
            fold = lambda i: 1 << (i - 1) if i > 1 else i  # noqa: E731
            roots = [(ram.s32(base + 0xF68), ram.s32(base + 0xF70)) for base in header['fighters']]
            if pair_roots is not None:
                pair_total += 1
                for i in range(3):
                    for j in range(i + 1, 3):
                        dx, dz = pair_roots[j][0] - pair_roots[i][0], pair_roots[j][1] - pair_roots[i][1]
                        if ram.u32(0x8009EA08 + 0x10 * (fold(i) + fold(j))) != math.isqrt(dx * dx + dz * dz):
                            pair_bad += 1
            pair_roots = roots
        previous = vec
    if args.pairs:
        print(f'pair table: {pair_total} frames compared, {pair_bad} pair distances differ')
    if trig:
        print(f'camera: {cam_total} frames compared, {cam_bad} differ')
    print(f'{total} frames compared; fields differing: {dict(bad.most_common(12))}')


if __name__ == '__main__':
    main()
