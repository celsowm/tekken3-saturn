#!/usr/bin/env python3
"""Continuous original traces of authored flight, juggle, landing and recovery input."""
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
    specs = [('parity-launch-candidate', 51, 0, 125),
             ('parity-launch-control', 0, 0, 140),
             ('parity-launch-hopkick', 52, 0, 125),
             ('parity-launch-juggle', 93, 36, 122),
             ('parity-recovery-mash', 48, 0, 125),
             ('parity-recovery-early-mash', 51, 0, 125)]
    if any(not (ROOT / 'build' / name / 'seed.json').is_file() for name, *_ in specs):
        print('private original air/recovery fixtures absent; skipped')
        return 77
    observed = {}
    for name, air_expected, ballistic_expected, health in specs:
        case = ROOT / 'build' / name
        rows, code = fight_oracle_eval.run(case, args.runner.resolve(), initialize_match=True)
        if code or len(rows) != 600 or not all(row['passed'] for row in rows):
            first = next((row['source_frame'] for row in rows if not row['passed']), None)
            raise AssertionError(f'{name}: {len(rows)}/600 ticks, first difference {first}')
        if any(row['unsupported'][i][0] for row in rows for i in range(2)):
            raise AssertionError(f'{name}: dropped move')
        if [row['source_frame'] for row in rows] != list(range(2, 602)):
            raise AssertionError(f'{name}: filtered or shifted source frames')
        air = ballistic = 0
        landing_frames = []
        for frame in range(2, 602):
            path = case / f'frame-{frame}.ranges'
            b = 0x800AAAB4
            air += read_field(path, b + 0xDB, 'B') != 0
            ballistic += read_field(path, b + 0x92, '<h') != 0
            if read_field(path, b + 0xDD, 'B'):
                landing_frames.append(frame)
        if (air, ballistic) != (air_expected, ballistic_expected):
            raise AssertionError(f'{name}: wrong original coverage {air}/{ballistic}')
        end = case / 'frame-601.ranges'
        original_health = [read_field(end, b + 0x3F4, '<i') for b in (0x800A9228, 0x800AAAB4)]
        if original_health != [140*65536, health*65536]:
            raise AssertionError(f'{name}: wrong original damage outcome {original_health}')
        if ballistic and 375 not in landing_frames:
            raise AssertionError('juggle fixture must reach original ballistic landing')
        observed[name] = dict(matched=600, air_ticks=air, ballistic_ticks=ballistic,
                              first_landing=landing_frames[0] if landing_frames else None)
    baseline = ROOT / 'build/parity-launch-candidate/frame-303.ranges'
    mash = ROOT / 'build/parity-recovery-early-mash/frame-303.ranges'
    if (read_field(baseline, 0x800AAAB4 + 0x118, '<h'),
            read_field(mash, 0x800AAAB4 + 0x118, '<h')) != (7, 0):
        raise AssertionError('early RP fixture must accelerate the nonzero recovery counter')
    late = ROOT / 'build/parity-recovery-mash/frame-346.ranges'
    if read_field(late, 0x800AAAB4 + 0xA0, '<H') != 3421:
        raise AssertionError('late RP fixture must exercise the original recovery branch')
    print(json.dumps(observed, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
