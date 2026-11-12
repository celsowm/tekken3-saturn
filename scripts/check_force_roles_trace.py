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


def dump_inputs(k, step, after):
    """One line per frame: the player's opponent and the wait before, then per enemy record its hidden flag
    (+0xC2), stored screen x (+0x1278), pair distance to the player and the player's quadrant and relative angle
    toward it; the opponent and wait the original left after the frame."""
    import fight_sim
    player = fs.F0
    parts = [f'frame {k}: opp {step.u8(player + 0x20)} wait {step.s32(fs.TARGET_WAIT)}']
    for e in (fs.F1, fs.F2):
        saved = [step.u16(player + o) for o in fs.ANGLES] + [step.u16(player + o + 0xE) for o in fs.ANGLES]
        fight_sim.relative_angles(fight_sim.Fighter(step, player), fight_sim.Fighter(step, e))
        q, r = step.s16(player + 0x32), step.s16(player + 0x30)
        for n, o in enumerate(fs.ANGLES):
            step.put(player + o, 'H', saved[n])
            step.put(player + o + 0xE, 'H', saved[n + 6])
        d = fs._pair_distance(step, step.u8(player + 0x1E), step.u8(e + 0x1E))
        parts.append(f'e{step.u8(e + 0x1E)} hid {step.u8(e + 0xC2)} sx {step.u16(e + 0x1278)} d {d} q {q} rel {r}')
    parts.append(f'-> opp {after.u8(player + 0x20)} wait {after.s32(fs.TARGET_WAIT)}')
    print(' | '.join(parts))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--fixed', action='store_true', help='run the port with game bug #47 fixed, as the traces have it')
    parser.add_argument('--max-frames', type=int, default=0)
    parser.add_argument('--show', type=int, default=6, help='print this many differing frames')
    parser.add_argument('--dump', type=int, nargs=2, metavar=('FROM', 'TO'),
                        help="print the choice's inputs (the state frame k-1 left) for trace frames FROM..TO")
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
            if args.dump and args.dump[0] <= k <= args.dump[1]:
                dump_inputs(k, step, ram)
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
