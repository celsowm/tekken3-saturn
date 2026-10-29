#!/usr/bin/env python3
"""Replay both original throw outcomes from configuration, with tap-state checks."""
import argparse
from collections import Counter
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import fight_oracle_eval


def read_field(path, address, fmt):
    data = path.read_bytes()
    if data[:8] != b'T3RANGES':
        raise AssertionError('invalid original snapshot')
    offset = 12
    for _ in range(struct.unpack_from('<I', data, 8)[0]):
        start, size = struct.unpack_from('<II', data, offset)
        offset += 8
        if start <= address and address + struct.calcsize(fmt) <= start + size:
            return struct.unpack_from(fmt, data, offset + address - start)[0]
        offset += size
    raise AssertionError(f'missing original field {address:#x}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', required=True, type=Path)
    parser.add_argument('--timeout', action='store_true',
                        help='check first round through timeout and the second intro instead')
    args = parser.parse_args()
    cases = [ROOT / 'build' / f'parity-escape-{name}' for name in ('control', 'candidate')]
    if args.timeout:
        cases = [ROOT / 'build/parity-round-timeout-oracle']
    if any(not (case / 'seed.json').is_file() for case in cases):
        print('private original continuous fight fixtures absent; skipped')
        return 77
    for case in cases:
        rows, code = fight_oracle_eval.run(case, args.runner.resolve(), initialize_match=True)
        ticks = 3000 if args.timeout else 460
        if code or len(rows) != ticks or not all(row['passed'] for row in rows):
            first = next((i for i, row in enumerate(rows) if not row['passed']), len(rows))
            raise AssertionError(f'{case.name}: compared {len(rows)}/{ticks}; first difference {first}')
        if not all(row.get('tap_previous_checked') and row.get('tap_checked') for row in rows):
            raise AssertionError('all ticks must check complete tap latch state')
        if any(row['unsupported'][i][0] for row in rows for i in range(2)):
            raise AssertionError('the original throw tape must not drop any move')
        if args.timeout:
            states, played = Counter(), Counter()
            for path in case.glob('frame-*.ranges'):
                states[read_field(path, 0x80096F2C, '<i')] += 1
                played[read_field(path, 0x80095484, '<i')] += 1
            if not {0, 1, 2, 4, 6, 9} <= states.keys() or set(played) != {1, 2}:
                raise AssertionError(f'missing original round transitions: {states}, {played}')
            print(json.dumps({'case': case.name, 'matched': len(rows), 'round_states': states,
                              'round_numbers': played, 'full_tap_state_ticks': len(rows),
                              'initial_state_copied': False}))
            continue
        # Require evidence of the intended branch, so a neutral fixture cannot pass.
        path = case / 'frame-317.ranges'
        slots = [read_field(path, b + 0xA0, '<H') for b in (0x800A9228, 0x800AAAB4)]
        expected = [2848, 3143] if case.name.endswith('control') else [3064, 2899]
        if slots != expected:
            raise AssertionError(f'{case.name}: original branch {slots} != {expected}')
        print(json.dumps({'case': case.name, 'matched': len(rows), 'full_tap_state_ticks': len(rows),
                          'original_slots_at_317': slots, 'initial_state_copied': False}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
