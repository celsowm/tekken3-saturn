#!/usr/bin/env python3
"""Summarize saved probe, combat-suite and Mednafen runs; compare cadence."""
import argparse
import json
from pathlib import Path

from saturn_probe import decode_panic


def read_run(path):
    if path.is_dir():
        candidates = [path / name for name in ('suite.json', 'observation.json', 'run.json')
                      if (path / name).is_file()]
        if len(candidates) != 1:
            raise ValueError(f'{path}: specify one suite.json, observation.json or run.json')
        path = candidates[0]
    return path, json.loads(path.read_text())


def probe_summary(data, directory):
    state = data['state']
    fields = ('frame_serial', 'submitted_frames', 'vblanks', 'sim_ticks',
              'first_error', 'draw_timeouts', 'commands_used', 'commands_capacity')
    result = {key: state[key] for key in fields}
    result.update({key:state[key] for key in (
        'observed_fields','discarded_fields','scheduled_ticks') if key in state})
    panic_file = directory / 'native-panic.bin'
    panic = decode_panic(panic_file.read_bytes()) if panic_file.exists() else None
    result['native_panic'] = panic
    result['passed'] = (not state['first_error'] and not state['draw_timeouts']
                        and state['commands_used'] <= state['commands_capacity']
                        and panic is None)
    result['elf_sha256'] = data['elf_sha256']
    result['cd_boot_validated'] = data['cd_boot_validated']
    return result


def summarize(path):
    path, data = read_run(path)
    if 'state' in data:
        result = dict(kind='probe', **probe_summary(data, path.parent))
    elif 'cases' in data and 'passed' in data:
        cases = {name: probe_summary(case, path.parent / name)
                 for name, case in data['cases'].items()}
        result = dict(kind='combat-suite', cases=len(cases),
                      failed_cases=[name for name, case in cases.items() if not case['passed']],
                      passed=bool(cases) and data['passed'] and all(c['passed'] for c in cases.values()))
    elif 'returncode' in data and 'seconds_requested' in data:
        result = dict(kind='mednafen', passed=data['returncode'] == 0,
                      returncode=data['returncode'], seconds_requested=data['seconds_requested'],
                      scope=data['scope'])
    else:
        raise ValueError(f'{path}: unsupported run format')
    return dict(path=str(path), **result)


def cadence(start, end, hz):
    if hz <= 0:
        raise ValueError('refresh rate must be positive')
    a, b = summarize(start), summarize(end)
    if a['kind'] != 'probe' or b['kind'] != 'probe' or not a['passed'] or not b['passed']:
        raise ValueError('cadence requires two successful probe endpoints')
    if not a['elf_sha256'] or a['elf_sha256'] != b['elf_sha256']:
        raise ValueError('cadence endpoints must use the same ELF')
    delta = {k: b[k] - a[k] for k in ('vblanks', 'submitted_frames', 'sim_ticks')}
    if delta['vblanks'] <= 0 or any(v < 0 for v in delta.values()):
        raise ValueError('cadence endpoints must advance without counter resets')
    scheduling=None
    if all(key in run for run in (a,b) for key in (
            'observed_fields','discarded_fields','scheduled_ticks')):
        scheduling={key:b[key]-a[key] for key in (
            'observed_fields','discarded_fields','scheduled_ticks')}
        if (scheduling['observed_fields']<=0 or
                not 0<=scheduling['discarded_fields']<=scheduling['observed_fields'] or
                scheduling['scheduled_ticks']<0):
            raise ValueError('scheduling counters must advance without resets/wrap')
        scheduling['discarded_percent']=100*scheduling['discarded_fields']/scheduling['observed_fields']
        scheduling['discarded_seconds']=scheduling['discarded_fields']/hz
    return dict(refresh_hz=hz, interval=delta, scheduling=scheduling,
                submissions_per_second=delta['submitted_frames'] * hz / delta['vblanks'],
                logic_ticks_per_second=delta['sim_ticks'] * hz / delta['vblanks'],
                scope='Uninstrumented endpoints are required for gameplay cadence; submitted frames are not verified display swaps.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', type=Path, nargs='*')
    parser.add_argument('--cadence', type=Path, nargs=2, metavar=('START', 'END'))
    parser.add_argument('--hz', type=float, default=60)
    args = parser.parse_args()
    if not args.runs and not args.cadence:
        parser.error('provide saved runs or --cadence START END')
    try:
        runs = [summarize(path) for path in args.runs]
        result = dict(runs=runs, passed=all(run['passed'] for run in runs))
        if args.cadence:
            result['cadence'] = cadence(*args.cadence, args.hz)
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f'{error}\n')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
