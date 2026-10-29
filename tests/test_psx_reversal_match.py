#!/usr/bin/env python3
"""Continuous original comparison of a reversal, its control and first-hit negative."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import fight_oracle_eval
from test_psx_throw_escape import read_field


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True, type=Path)
    args = parser.parse_args()
    specs = [('full', 600, 369, [3277, 3304]),
             ('control', 460, 369, [2693, 295]),
             ('first-hit', 360, 289, [2247, 295])]
    cases = [(ROOT / 'build' / f'parity-reversal-{name}', ticks, frame, slots)
             for name, ticks, frame, slots in specs]
    if any(not (case / 'seed.json').is_file() for case, *_ in cases):
        print('private original reversal fight fixtures absent; skipped')
        return 77
    for case, ticks, frame, slots in cases:
        rows, code = fight_oracle_eval.run(case, args.runner.resolve(), initialize_match=True)
        if code or len(rows) != ticks or not all(row['passed'] for row in rows):
            first = next((row['source_frame'] for row in rows if not row['passed']), None)
            raise AssertionError(f'{case.name}: {len(rows)}/{ticks} ticks; first difference {first}')
        if any(row['unsupported'][i][0] for row in rows for i in range(2)):
            raise AssertionError('reversal fixture must not drop unsupported moves')
        path = case / f'frame-{frame}.ranges'
        original_slots = [read_field(path, b + 0xA0, '<H') for b in (0x800A9228, 0x800AAAB4)]
        if original_slots != slots:
            raise AssertionError(f'{case.name}: original outcome {original_slots} != {slots}')
        if case.name.endswith('full'):
            before = case / 'frame-368.ranges'
            if read_field(before, 0x800A9228 + 0xEA, '<h') != 27:
                raise AssertionError('positive fixture must cover the original damage override')
            if read_field(before, 0x800A9228 + 0x1A0, '<H') != 3277:
                raise AssertionError('positive fixture must queue the actual reversal')
            end = case / 'frame-601.ranges'
            if read_field(end, 0x800A9228 + 0xA0, '<H') != 3:
                raise AssertionError('positive fixture must finish the reversal animation')
            if [read_field(end, b + 0x3F4, '<i') for b in (0x800A9228, 0x800AAAB4)] != [140*65536,113*65536]:
                raise AssertionError('positive fixture must cover all 27 damage without a hit on Paul')
        print(json.dumps({'case': case.name, 'matched': len(rows), 'original_slots': slots,
                          'initial_state_copied': False}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
