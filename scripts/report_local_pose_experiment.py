#!/usr/bin/env python3
"""Compare local FK/IK reuse matrices using validated raw profiler captures."""
import argparse
import json
from pathlib import Path

from analyze_simulation_profile import analyze
from compare_simulation_profiles import compare
from report_saturn_runs import cadence


def matrix_comparison(baseline, candidate):
    roots=(baseline,candidate)
    matrices=[json.loads((p/'matrix.json').read_text()) for p in roots]
    if not all(m.get('completed') for m in matrices):
        raise ValueError('incomplete simulation matrix')
    if matrices[0]['cases'].keys()!=matrices[1]['cases'].keys():
        raise ValueError('different workload cases')
    cases={}
    for name in matrices[0]['cases']:
        old,new=(m['cases'][name] for m in matrices)
        if old['input_sha256']!=new['input_sha256']:
            raise ValueError('different controller script: '+name)
        captures=[]
        for root in roots:
            profile=json.loads((root/name/'profile.json').read_text())
            capture=root/name/f"capture-{profile['publication_retries']}"
            if json.loads((capture/'observation.json').read_text())['elf_sha256']!=profile['elf_sha256']:
                raise ValueError('profile references a different ELF')
            captures.append(capture)
        data=[analyze(p) for p in captures]
        comparison=compare(*data)
        observed=[json.loads((p/'observation.json').read_text()) for p in captures]
        emulators=[json.loads((p/'ymir.json').read_text()) for p in captures]
        for key in ('sh2_cache_emulation','nominal_refresh_hz',
                    'sh2_clock_factor_num','sh2_clock_factor_den','master_ccr','slave_ccr'):
            if any(key not in e['boot'] for e in emulators) or emulators[0]['boot'][key]!=emulators[1]['boot'][key]:
                raise ValueError('different emulator policy: '+key)
        if any('division_timing_model' not in e for e in emulators) or emulators[0]['division_timing_model']!=emulators[1]['division_timing_model']:
            raise ValueError('different divider timing model')
        for o in observed:
            s=o['state']
            if s['first_error'] or s['draw_timeouts'] or s['psx_attack_decode_errors']!=[0,0]:
                raise ValueError('unhealthy gameplay capture')
        cases[name]=dict(
            diagnostic_ms_per_tick={stage:[d['stages'][stage]['inclusive_ms_per_tick'] for d in data]
                for stage in ('tick','compose','fk','ik','copy','branch')},
            tick_reduction_percent=comparison['stages']['tick']['inclusive']['reduction_percent'],
            pose_cache_snapshot=[o['state']['local_pose_cache'] for o in observed],
            aggregate_ticks=comparison['aggregate_ticks'],
            elf_sha256=[o['elf_sha256'] for o in observed])
    return dict(cases=cases,scope='Intrusive per-tick profiles; cache counters include startup. '
        'Identical field scripts can reach different logic ticks/poses/phases. '
        'Not behavior equivalence, release cadence, or hardware qualification.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True)
    p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--baseline-cadence',type=Path,nargs=2)
    p.add_argument('--candidate-cadence',type=Path,nargs=2)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    if bool(a.baseline_cadence)!=bool(a.candidate_cadence):p.error('supply both release intervals')
    report=matrix_comparison(a.baseline,a.candidate)
    if a.baseline_cadence:
        report['release_cadence']=[cadence(*paths,60) for paths in (a.baseline_cadence,a.candidate_cadence)]
    a.out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
