#!/usr/bin/env python3
"""Record the original practice overlay's input recorder in Tekken3Remake's CPU harness.

flow_trace.py's `practice` scenario only reaches the guide's READY, STAND and PLAY states (COMBO TRAINING).
This scenario opens the FREE page, closes it with L1+R1+L2+R2+Circle held (the original's switch for the input
recorder, practice_mode.gd put(GUIDE, ...)), then plays the recorder: down + Select starts RECORD_WAIT, a button
starts RECORDING, Select ends it, Select again replays it (REPLAY, REPLAY_END). A second recording runs the ring
full (RING_WORDS - 1 words end it without Select).

    third_party/Tekken3Remake/work/venv/bin/python scripts/practice_record_trace.py [--out FILE]

The output (default third_party/Tekken3Remake/work/traces/flow_practice_record.bin) is git-ignored game data;
scripts/export_practice_trace.py --trace FILE --out build/practice-record/cases.txt turns it into cases.
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'third_party/Tekken3Remake/tools/research'
sys.path.insert(0, str(RESEARCH))

import flow_trace as ft  # noqa: E402

RECORDER = 0x2F          # L2 R2 L1 R1 Circle held when the FREE page closes: the recorder is on
FULL_RING = [ft.SQUARE, 0, ft.CROSS, 0, ft.CIRCLE, 0, ft.TRIANGLE, 0] * 40   # more key changes than the ring holds


def scenario():
    base = next(s for s in ft.SCENARIOS if s.name == 'practice')
    to_select = len(ft._to_menu()) + len(ft._menu(6)) + 2     # the title, the menu entry, the select's two steps
    steps = list(base.steps[:to_select]) + [
        # MODE SELECT -> 1P FREESTYLE, then OK with the recorder keys held.
        (ft.paused(after=40), ft.presses(ft.CROSS)),
        (ft.paused(after=8), ft.presses(ft.CROSS | RECORDER)),
        # Record: down + Select, wait, a few inputs, Select ends it; Select again replays it.
        (ft.at(8, 8, after=60), ft.presses(ft.DOWN | ft.SELECT)),
        (ft.at(8, 8, after=20), ft.presses(ft.SQUARE, 0, 0, 0, ft.CROSS, 0, 0, 0, ft.CIRCLE, 0, 0, 0, ft.DOWN, 0, 0, 0, ft.TRIANGLE)),
        (ft.at(8, 8, after=30), ft.presses(ft.SELECT)),
        (ft.at(8, 8, after=30), ft.presses(ft.SELECT)),
        # After the replay: record again until the ring is full, then replay that one with Left held (resumed input).
        (ft.at(8, 8, after=240), ft.presses(ft.DOWN | ft.SELECT)),
        (ft.at(8, 8, after=20), ft.presses(*FULL_RING)),
        (ft.at(8, 8, after=120), ft.presses(ft.LEFT | ft.SELECT)),
    ]
    return ft.Scenario('practice_record', seed=19, frames=3600, steps=steps)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--out', type=Path, default=ROOT / 'third_party/Tekken3Remake/work/traces/flow_practice_record.bin')
    args = ap.parse_args()
    frames = ft.write_trace(scenario(), args.out)
    print(f'{frames} frames -> {args.out}')


if __name__ == '__main__':
    main()
