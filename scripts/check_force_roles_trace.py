#!/usr/bin/env python3
"""Step the verified Python `fighter_roles` (Tekken3Remake `force_sim.py`) over a recorded Tekken Force game
(`flow_trace.py force`) and compare who the player faces with the original's, frame by frame.

Each running Force frame k is stepped from the state recorded after frame k-1 (fighter records, target wait,
close flags, manual switch, the pair-distance table frame k-1 left: the roles run first in FightFrame) and compared with the player's opponent index and the target wait after
frame k.

`force_sim.py` reproduces game bug #47 (FUN_800B598C saves the player's second six angle fields in the first
half's place and restores both halves from the first). The harness that recorded the traces patches the bug
(`game_harness.OVERLAY_PATCHES["force"]`). `--fixed` runs the port with the bug fixed, to match the traces.
Run with the Tekken3Remake venv:

    third_party/Tekken3Remake/work/venv/bin/python scripts/check_force_roles_trace.py \
        --trace third_party/Tekken3Remake/work/traces/flow_force.bin [--fixed]
"""
import argparse
import inspect
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
sys.path.insert(0, str(ROOT / 'third_party/Tekken3Remake/tools/research'))

import check_force_flow_trace as cf   # noqa: E402
import force_sim as fs                # noqa: E402

QUADRANT_ORDER = (2, 0, 1, 0)   # force.ovl 0x800B0F04, s32[4]: what force_sim.py documents


def fix_bug_47():
    """force_sim.choose_target with the player's twelve angle fields saved and restored as the traces have it."""
    source = inspect.getsource(fs.choose_target)
    save = 'saved = [ram.u16(player + o) for o in ANGLES]\n'
    restore = 'ram.put(player + o + 0xE, "H", saved[k])'
    assert source.count(save) == 1 and source.count(restore) == 1
    source = source.replace(save, save + '                saved2 = [ram.u16(player + o + 0xE) for o in ANGLES]\n')
    source = source.replace(restore, 'ram.put(player + o + 0xE, "H", saved2[k])')
    exec(compile(source, 'force_sim_fixed', 'exec'), fs.__dict__)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--fixed', action='store_true', help='run the port with game bug #47 fixed, as the traces have it')
    parser.add_argument('--max-frames', type=int, default=0)
    parser.add_argument('--show', type=int, default=6, help='print this many differing frames')
    args = parser.parse_args()
    if args.fixed:
        fix_bug_47()
    header, body = cf.load_trace(args.trace)
    previous = None
    total = bad = shown = 0
    for k, (pads, ranges, records) in enumerate(cf.frames(header, body)):
        if args.max_frames and k >= args.max_frames:
            break
        if len(records) != 3:
            previous = None
            continue
        ram = cf.build_ram(header, ranges, records)
        if ram.s32(0x800AFF50) != 8 or ram.s32(0x80097350) != 1:
            previous = None
            continue
        if previous is not None:
            step = cf.build_ram(*previous)
            # the facing-quadrant order is overlay data, which the trace does not record
            for q, value in enumerate(QUADRANT_ORDER):
                step.put(fs.QUADRANT_ORDER + 4 * q, 'I', value)
            out = 0x80000000 | 0x1F0000
            fs.fighter_roles(step, out)
            player = step.u32(out)
            total += 1
            got = step.u8(player + 0x20)
            want = ram.u8(player + 0x20)
            if got != want or step.s32(fs.TARGET_WAIT) != ram.s32(fs.TARGET_WAIT):
                bad += 1
                if shown < args.show:
                    shown += 1
                    print(f'frame {k}: target {got}/{want}, wait {step.s32(fs.TARGET_WAIT)}/{ram.s32(fs.TARGET_WAIT)}')
        previous = (header, ranges, records)
    print(f'{total} frames compared, {bad} differ' + (' (bug #47 fixed)' if args.fixed else ' (bug #47 reproduced)'))


if __name__ == '__main__':
    main()
