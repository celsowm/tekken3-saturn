#!/usr/bin/env python3
"""Fresh owned matches against original code with remake presentation fixes undone."""
import argparse
import hashlib
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
    specs = [('escape', 460), ('reversal', 600), ('crossing', 500)]
    if any(not (ROOT / f'build/parity-retail-camera-{name}/seed.json').is_file() for name, _ in specs):
        print('private original retail camera fixtures absent; skipped')
        return 77
    # Fresh retail timeout also covers main-loop RNG while FightMain places
    # the next round without running the camera director for three frames.
    timeout = ROOT / 'build/parity-owned-timeout'
    if (timeout / 'seed.json').is_file():
        specs.append(('timeout', 3000))
    exe = ROOT / 'third_party/Tekken3Remake/work/jp_rev1/exe.bin'
    for name, ticks in specs:
        case = timeout if name == 'timeout' else ROOT / f'build/parity-retail-camera-{name}'
        meta = json.loads((case / 'seed.json').read_text())
        if meta.get('remake_presentation_fixes') is not False:
            raise AssertionError('retail camera proof requires explicit restoration provenance')
        if exe.is_file() and meta['original_exe_sha256'] != hashlib.sha256(exe.read_bytes()).hexdigest():
            raise AssertionError('original executable identity differs')
        rows, code = run(case, a.runner.resolve(), initialize_match=True, independent_camera=True)
        first = next((r for r in rows if not r['passed']), None)
        if code or len(rows) != ticks or first:
            raise AssertionError(f'{name}: {len(rows)}/{ticks}, exit {code}, first {first}')
        if [r['source_frame'] for r in rows] != list(range(2, ticks + 2)):
            raise AssertionError('filtered/shifted original frames')
        if not all(r['camera_owned'] and r['physical_inputs'] and r['camera_rng_checked'] and r['camera_targets_checked'] and
                   r['sound_checked'] and r['supported'] and not any(f[0] for f in r['unsupported']) for r in rows):
            raise AssertionError('missing original state, failed simulation or dropped move')
        if name == 'crossing':
            at = {r['source_frame']:r for r in rows}
            if [at[n]['input_direction'][0] for n in (430,431,432,433)] != [6,6,4,4]:
                raise AssertionError('physical right must cross from toward to away after this draw')
        print(json.dumps(dict(case=name, matched=ticks, remake_presentation_fixes=False,
                              physical_input_phase='after_draw')))
    return 0


if __name__ == '__main__':
    sys.exit(main())
