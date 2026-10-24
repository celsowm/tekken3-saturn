#!/usr/bin/env python3
"""Run t3_match_replay_eval on a fight_oracle_export.py case; report the first divergence.

Prints the matching prefix, the first differing tick (with its inputs and the
differences), and every move the native match dropped as unsupported.
"""
import argparse
import collections
import hashlib
import json
import os
import struct
import subprocess
from pathlib import Path

from build_reference_replay import VIEW_BYTES, VIEW_HEADER

ROOT = Path(__file__).resolve().parents[1]


def run(case, evaluator, export=None):
    meta = json.loads((case / 'seed.json').read_text())
    args = [str(evaluator), str(case / 'seed.bin'), str(case / 'cases.tsv'),
            *(hex(meta[k]) for k in ('step_x', 'step_z', 'previous', 'ramp')), '--original-round-flow']
    env = dict(os.environ)
    if export is not None:
        export.mkdir(parents=True, exist_ok=True)
        for name in ('manifest.json', 'views.bin'):
            (export / name).unlink(missing_ok=True)
        env['T3_REFERENCE_OUTPUT'] = str(export.resolve())
    out = subprocess.run(args, capture_output=True, text=True, env=env)
    (case / 'native.jsonl').write_text(out.stdout)
    (case / 'native.stderr').write_text(out.stderr)
    rows = [json.loads(line) for line in out.stdout.splitlines()]
    return rows, out.returncode


def write_reference_manifest(case, evaluator, export, rows):
    """Publish a Saturn fixture only after every original-code tick matched."""
    names = ['seed.ranges', 'seed-addresses.json', 'inputs.tsv', 'expected.sha256']
    views = export / 'views.bin'
    if views.exists():
        data = views.read_bytes()
        if len(data) < VIEW_HEADER + 4 or data[:8] != b'T3VIEWS\0':
            raise ValueError('reference draw trace is truncated')
        count = struct.unpack_from('<I', data, 8)[0]
        if not 1 <= count <= 256 or \
                len(data) != VIEW_HEADER + VIEW_BYTES * count + 4 + 4 * len(rows):
            raise ValueError('reference view count differs from the original fight')
        ticks = [struct.unpack_from('<I', data, VIEW_HEADER + VIEW_BYTES * i)[0]
                 for i in range(count)]
        if ticks != sorted(set(ticks)) or ticks[-1] >= len(rows):
            raise ValueError('reference view ticks are not ordered')
        if struct.unpack_from('<I', data, VIEW_HEADER + VIEW_BYTES * count)[0] != len(rows):
            raise ValueError('reference screen count differs from the original fight')
        names.append('views.bin')
    files = {name: hashlib.sha256((export / name).read_bytes()).hexdigest() for name in names}
    if len((export / 'expected.sha256').read_bytes()) != 32 * len(rows):
        raise ValueError('reference digest count differs from the original fight')
    if len((export / 'inputs.tsv').read_text().splitlines()) != len(rows):
        raise ValueError('reference input count differs from the original fight')
    source = hashlib.sha256()
    for path in (case / 'seed.json', case / 'seed.bin', case / 'cases.tsv',
                 *sorted(case.glob('frame-*.ranges'), key=lambda p: int(p.stem.split('-')[1]))):
        source.update(path.name.encode() + b'\0')
        source.update(hashlib.sha256(path.read_bytes()).digest())
    schema_files = ('tools/psx_match_reference.inc', 'tools/psx_ground_eval_fields.h',
                    'tools/psx_round_flow_eval_fields.h')
    manifest = dict(schema=1, codec='sha256-i64be-v1', ticks=len(rows),
                    source='original-code FightMain via pinned Tekken3Remake Unicorn harness',
                    seed_sha256=hashlib.sha256((case / 'seed.bin').read_bytes()).hexdigest(),
                    trace_sha256=source.hexdigest(),
                    evaluator_sha256=hashlib.sha256(evaluator.read_bytes()).hexdigest(),
                    audit_passed=True, files=files,
                    observation_schema_sha256=hashlib.sha256(b''.join((ROOT / n).read_bytes()
                                                               for n in schema_files)).hexdigest())
    (export / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('case', type=Path)
    p.add_argument('--evaluator', type=Path, default=ROOT / 'build/host/t3_match_replay_eval')
    p.add_argument('--show', type=int, default=12, help='differences to print')
    p.add_argument('--trace', action='store_true',
                   help='use build/fail-trace (scripts/build_fail_trace_eval.py) and print the refusal chain')
    p.add_argument('--export', type=Path,
                   help='write a private Saturn reference fixture only if all original-code ticks match')
    args = p.parse_args()
    if args.trace:
        args.evaluator = ROOT / 'build/fail-trace/t3_match_replay_eval'
    rows, code = run(args.case, args.evaluator, args.export)
    tape = (args.case / 'cases.tsv').read_text().splitlines()
    dropped = collections.Counter()
    seen = [0, 0]
    for r in rows:
        for i, (count, slot, transition) in enumerate(r.get('unsupported', [[0, 0, 0]] * 2)):
            if count > seen[i]:
                dropped[(i, slot, transition)] += count - seen[i]
                seen[i] = count
    first = next((i for i, r in enumerate(rows) if not r['passed']), None)
    print(json.dumps(dict(case=str(args.case), exit=code, ticks=len(rows), tape=len(tape),
                          matching_prefix=len(rows) if first is None else first)))
    if first is not None:
        r = rows[first]
        print('tick', first, 'source frame', r['source_frame'], 'supported', r['supported'])
        for k in range(max(0, first - 3), first + 1):
            print('  input', k, tape[k].split('\t')[1:])
        for d in r['differences'][:args.show]:
            print('  ', d)
    if args.trace and rows and not rows[-1]['supported']:
        chain = (args.case / 'native.stderr').read_text().splitlines()[-8:]
        print('refusal chain (innermost first):', ' <- '.join(line.split()[-1] for line in chain))
    for (i, slot, transition), n in sorted(dropped.items()):
        print(f'dropped: fighter {i} slot {slot} transition {transition} x{n}')
    passed = code == 0 and first is None and len(rows) == len(tape)
    if passed and args.export is not None:
        write_reference_manifest(args.case, args.evaluator, args.export, rows)
    return 0 if passed else 1


if __name__ == '__main__':
    raise SystemExit(main())
