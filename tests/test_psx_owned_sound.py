#!/usr/bin/env python3
"""Sound/eye/shared RNG coverage, preserving the failing full camera audit.

This explicitly tests a component, not full fight or independent camera parity.
The evaluator still writes every difference to native-camera.jsonl.
"""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from fight_oracle_eval import run


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner', required=True, type=Path)
    a = p.parse_args()
    case = ROOT / 'build/parity-sounds-escape'
    if not (case / 'seed.json').is_file():
        print('private original sound/escape fixture absent; skipped')
        return 77
    rows, code = run(case, a.runner.resolve(), initialize_match=True, independent_camera=True)
    if code or len(rows) != 460 or [r['source_frame'] for r in rows] != list(range(2, 462)):
        raise AssertionError('sound fixture must execute all 460 original ticks')
    if not all(r['camera_owned'] and r['camera_rng_checked'] and r['sound_checked'] and r['supported']
               and not any(f[0] for f in r['unsupported']) for r in rows):
        raise AssertionError('missing original fields, dropped move or failed owned simulation')
    for row in rows:
        for diff in row['differences']:
            if diff['category'] in ('sound', 'animation') or (diff['category'] == 'camera' and
                    diff['field'] in ('rand', 'camera_rand', 'ai_rand')):
                raise AssertionError(f"source {row['source_frame']}: {diff}")
    first = next((r['source_frame'] for r in rows if not r['passed']), None)
    print(json.dumps(dict(sound_eye_rng_matched=460, full_audit_first_difference=first,
                          full_audit_passed=first is None)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
