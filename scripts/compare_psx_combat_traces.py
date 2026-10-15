#!/usr/bin/env python3
"""Diff independent PS1 and Saturn MoveVM combat observations by frame.

The script NEVER extracts retail data; users supply their own private JSONL.
Each observed tick requires two fighter rows. All comparable optional fields
must be present on BOTH sides or neither side, avoiding silent omissions.
"""
import argparse
import json
import sys
from pathlib import Path

REQUIRED = ('tick', 'fighter', 'move_slot', 'pose_frame', 'health_q16')
OPTIONAL = ('phase', 'phase_frame', 'frames_left', 'combo_hits_received',
            'hit_slot', 'ko')
LIMITS = {
    'tick': (0, 0xFFFFFFFF), 'fighter': (0, 1),
    'move_slot': (0, 4022), 'pose_frame': (0, 0xFFFF),
    'health_q16': (0, 0x7FFFFFFF), 'phase': (0, 5),
    'phase_frame': (0, 0xFFFF), 'frames_left': (0, 0xFFFF),
    'combo_hits_received': (0, 0xFFFF), 'hit_slot': (-1, 1),
    'ko': (0, 1),
}
MAX_ROWS = 400000


class InvalidTrace(ValueError):
    pass


def load(path):
    records = {}
    previous_tick = None
    with Path(path).open('r', encoding='utf-8') as stream:
        for line_number, raw in enumerate(stream, 1):
            if not raw.strip():
                continue
            if len(records) >= MAX_ROWS:
                raise InvalidTrace(f'{path}: too many rows (limit {MAX_ROWS})')
            try:
                row = json.loads(raw)
            except json.JSONDecodeError as exc:
                raise InvalidTrace(f'{path}:{line_number}: invalid JSON: {exc}') from exc
            if not isinstance(row, dict):
                raise InvalidTrace(f'{path}:{line_number}: expected a JSON object')
            missing = [key for key in REQUIRED if key not in row]
            if missing:
                raise InvalidTrace(f'{path}:{line_number}: missing {missing}')
            for key in REQUIRED + OPTIONAL:
                if key not in row:
                    continue
                lo, hi = LIMITS[key]
                value = row[key]
                if type(value) is not int or not lo <= value <= hi:
                    raise InvalidTrace(
                        f'{path}:{line_number}: {key} must be integer in [{lo}, {hi}]')
            key = (row['tick'], row['fighter'])
            if key in records:
                raise InvalidTrace(f'{path}:{line_number}: duplicate tick/fighter {key}')
            if previous_tick is not None and row['tick'] < previous_tick:
                raise InvalidTrace(f'{path}:{line_number}: ticks are out of order')
            previous_tick = row['tick']
            records[key] = {field: row[field] for field in REQUIRED + OPTIONAL
                            if field in row}
    if not records:
        raise InvalidTrace(f'{path}: empty trace')
    ticks = sorted({tick for tick, _ in records})
    if ticks[-1] - ticks[0] + 1 != len(ticks):
        raise InvalidTrace(f'{path}: missing intermediate tick in {ticks[0]}..{ticks[-1]}')
    for tick in ticks:
        if (tick, 0) not in records or (tick, 1) not in records:
            raise InvalidTrace(f'{path}: missing P1/P2 observation at tick {tick}')
    return records


def compare(ps1, saturn):
    errors = []
    pkeys, skeys = set(ps1), set(saturn)
    for tick, fighter in sorted(pkeys - skeys):
        errors.append(f'tick {tick} P{fighter + 1}: missing Saturn observation')
    for tick, fighter in sorted(skeys - pkeys):
        errors.append(f'tick {tick} P{fighter + 1}: unexpected Saturn observation')
    for tick, fighter in sorted(pkeys & skeys):
        expected, actual = ps1[(tick, fighter)], saturn[(tick, fighter)]
        for field in REQUIRED[2:] + OPTIONAL:
            if field not in expected and field not in actual:
                continue
            if field not in expected or field not in actual:
                errors.append(f'tick {tick} P{fighter + 1} {field}: missing from '
                              f'{"PS1" if field not in expected else "Saturn"}')
            elif expected[field] != actual[field]:
                errors.append(f'tick {tick} P{fighter + 1} {field}: '
                              f'PS1={expected[field]} Saturn={actual[field]}')
    return errors


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ps1', type=Path, required=True,
                        help='Independent original PlayStation JSONL capture')
    parser.add_argument('--saturn', type=Path, required=True,
                        help='Projected or observed Saturn JSONL capture')
    parser.add_argument('--max-errors', type=int, default=20)
    args = parser.parse_args(argv)
    if not 1 <= args.max_errors <= 10000:
        parser.error('--max-errors must be in 1..10000')
    try:
        reference, output = load(args.ps1), load(args.saturn)
    except (OSError, InvalidTrace) as exc:
        print(f'Invalid trace: {exc}', file=sys.stderr)
        return 2
    errors = compare(reference, output)
    if errors:
        for error in errors[:args.max_errors]:
            print(error)
        print(f'FAIL: {len(errors)} mismatches over '
              f'{len(reference) // 2} PS1 ticks / {len(output) // 2} Saturn ticks')
        return 1
    print(f'PASS: {len(reference) // 2} ticks, 2 fighters, exact fields')
    return 0


if __name__ == '__main__':
    sys.exit(main())
