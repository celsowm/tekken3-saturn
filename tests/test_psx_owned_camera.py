#!/usr/bin/env python3
"""Strict fresh-match camera comparison on private original air/timeout traces.

Throw/reversal cases with known differences are documented separately; they
must not be reclassified as passing camera coverage.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from fight_oracle_eval import run


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True, type=Path)
    parser.add_argument('--full-state', action='store_true',
                        help='require fresh sources, director and all three RNG captures')
    args = parser.parse_args()
    specs = [(name, 600) for name in (
        'parity-launch-candidate', 'parity-launch-control', 'parity-launch-hopkick',
        'parity-launch-juggle', 'parity-recovery-mash', 'parity-recovery-early-mash')]
    specs.append(('parity-round-timeout-oracle', 3000))
    if args.full_state:
        specs = [('parity-sounds-neutral', 600)]
    if any(not (ROOT / 'build' / name / 'seed.json').is_file() for name, _ in specs):
        print('private original camera fixtures absent; skipped')
        return 77
    for name, ticks in specs:
        rows, code = run(ROOT / 'build' / name, args.runner.resolve(),
                         initialize_match=True, independent_camera=True)
        first = next((row for row in rows if not row['passed']), None)
        if code or len(rows) != ticks or first:
            raise AssertionError(f'{name}: {len(rows)}/{ticks}, exit {code}, first {first}')
        if not all(row['camera_owned'] and row['supported'] for row in rows):
            raise AssertionError('camera must be owned on every tick')
        if args.full_state and not all(row.get('camera_rng_checked') for row in rows):
            raise AssertionError('internal camera and RNG fields must be checked on every tick')
        if args.full_state and not all(row.get('sound_checked') for row in rows):
            raise AssertionError('sound scripts/timers must be checked on every tick')
        if [row['source_frame'] for row in rows] != list(range(2, ticks + 2)):
            raise AssertionError('filtered or shifted source frames')
        if any(row['unsupported'][i][0] for row in rows for i in range(2)):
            raise AssertionError('dropped move in camera comparison')
        print(json.dumps(dict(case=name, matched=ticks, camera_owned=True)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
