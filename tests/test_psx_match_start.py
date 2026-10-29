#!/usr/bin/env python3
"""Continuous first-round comparison with original FightMain fixtures.

The native core starts from configuration, with no fighter/RAM seeding.
Camera screen positions remain an explicit external fact, as in the existing
combat oracle; this check does not certify independent camera equality.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
import fight_oracle_eval


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', type=Path, required=True)
    args = parser.parse_args()
    cases = [ROOT / 'build' / name for name in ('parity-intro-oracle', 'parity-intro-command-oracle',
                                              'parity-intro-contact-oracle')]
    if any(not (case / 'seed.json').is_file() for case in cases):
        print('private first-round original-code fixtures absent; skipped')
        return 77
    for case in cases:
        rows, code = fight_oracle_eval.run(case, args.runner.resolve(), initialize_match=True)
        expected = len((case / 'cases.tsv').read_text().splitlines())
        if code or len(rows) != expected or not rows or not all(row['passed'] for row in rows):
            first = next((i for i, row in enumerate(rows) if not row['passed']), len(rows))
            raise AssertionError(f'{case.name}: compared {len(rows)}/{expected}; first difference {first}')
        hud_ticks = sum(bool(row.get('hud_checked')) for row in rows)
        if case.name == 'parity-intro-contact-oracle':
            if hud_ticks != expected:
                raise AssertionError('the HUD fixture must compare both bars on every tick')
            if not any(row['hud_health'] != row['fighter_health'] for row in rows):
                raise AssertionError('the HUD fixture must cover damage after the HUD phase')
        print(json.dumps({'case': case.name, 'matched': len(rows), 'hud_ticks': hud_ticks,
                          'initial_state_copied': False}))
    return 0


if __name__ == '__main__':
    sys.exit(main())
