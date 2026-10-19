#!/usr/bin/env python3
"""Compare validated nested captures, keeping diagnostic costs separate from release cadence."""
import argparse,json
from pathlib import Path
from analyze_simulation_profile import analyze
from report_saturn_runs import cadence


def compare(before,after):
    if before['after_field']!=after['after_field']:
        raise ValueError('different profiling warmup')
    for key in ('bios_sha256','probe_sha256'):
        if not before['provenance'].get(key) or before['provenance'][key]!=after['provenance'].get(key):
            raise ValueError('incompatible profiler provenance: '+key)
    if before['stages'].keys()!=after['stages'].keys():raise ValueError('different stage schema')
    rows={}
    for name,old in before['stages'].items():
        new=after['stages'][name]
        if old['kind']!=new['kind']:raise ValueError('different stage kind')
        row=dict(kind=old['kind'],before_calls_per_tick=old['calls']/before['aggregate_ticks'],
                 after_calls_per_tick=new['calls']/after['aggregate_ticks'])
        for kind in ('inclusive','exclusive'):
            key=kind+'_ms_per_tick';a,b=old[key],new[key]
            row[kind]=dict(before_ms_per_tick=a,after_ms_per_tick=b,
                          reduction_percent=(1-b/a)*100 if a else None)
        rows[name]=row
    return dict(stages=rows,aggregate_ticks=[before['aggregate_ticks'],after['aggregate_ticks']],
        scope='Diagnostic per-tick costs include hooks, interrupts and changed code/cache layout. '
        'Inclusive stages overlap. Workload equivalence and correctness are not certified by this comparison. '
        'Fixed-field inputs may advance different logic ticks. No hardware qualification; '
        'the pinned Ymir DIVU model remains immediate.')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True,help='capture directory containing raw symbol dumps')
    p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--baseline-cadence',type=Path,nargs=2)
    p.add_argument('--candidate-cadence',type=Path,nargs=2)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    if bool(a.baseline_cadence)!=bool(a.candidate_cadence):p.error('supply both cadence endpoint pairs')
    baseline=analyze(a.baseline)
    result=compare(baseline,analyze(a.candidate))
    if a.baseline_cadence:
        old,new=[cadence(*pair,60) for pair in (a.baseline_cadence,a.candidate_cadence)]
        for pair in (a.baseline_cadence,a.candidate_cadence):
            for path in pair:
                observation=json.loads((path/'observation.json').read_text())
                if any(observation['provenance'].get(k)!=result_value for k,result_value in
                       baseline['provenance'].items() if k in ('bios_sha256','probe_sha256')):
                    raise ValueError('cadence/profiler provenance differs')
        if old['submissions_per_second']<=0:raise ValueError('baseline cadence did not advance')
        result['release_cadence']=dict(before=old,after=new,improvement_percent=
            (new['submissions_per_second']/old['submissions_per_second']-1)*100,
            scope='Separate saved endpoints; callers must establish uninstrumented builds and matching '
            'runtime configuration. This report does not certify source equivalence or distinct display swaps.')
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(a.out)

if __name__=='__main__':main()
