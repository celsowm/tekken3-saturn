#!/usr/bin/env python3
"""Print the timing counters and round state of combat-suite cases.

Compares how simulation ticks advance against emulator fields between runs,
for example when a suite check that samples a fixed number of fields later
starts failing.

    combat_case_counters.py assets/imported/<suite> [more suites] --cases ko-victory ko-victory-hold
"""
import argparse
import json
from pathlib import Path

KEYS = ('frames', 'observed_fields', 'discarded_fields', 'sim_ticks', 'scheduled_ticks', 'frame_serial',
        'psx_round_state', 'psx_replay_mode', 'psx_round_counter', 'psx_move_slot', 'psx_move_frame')


def find(d, key):
    if isinstance(d, dict):
        if key in d:
            return d[key]
        for v in d.values():
            r = find(v, key)
            if r is not None:
                return r
    return None


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('suites', nargs='+', type=Path)
    p.add_argument('--cases', nargs='+', required=True)
    a = p.parse_args()
    for suite in a.suites:
        for case in a.cases:
            path = suite/case/'observation.json'
            if not path.exists():
                print(f'{suite.name}/{case}: missing')
                continue
            d = json.loads(path.read_text())
            print(f'{suite.name}/{case}: ' + ' '.join(f'{k}={find(d, k)}' for k in KEYS))


if __name__ == '__main__':
    main()
