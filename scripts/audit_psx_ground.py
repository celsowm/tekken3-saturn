#!/usr/bin/env python3
"""Compare native ground physics with real before/after CPU RAM range captures.

Each component evaluation is seeded independently with observed state. This
does not establish free-running match/round, pose, or Saturn combat parity.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from analyze_psx_phases import analyze


def audit(capture,evaluator,tables,starts=False):
    trace=capture/'phases.jsonl'
    rows=[json.loads(l) for l in trace.read_text().splitlines()];analyze(rows)
    provenance=json.loads((capture/'provenance.json').read_text())
    name='MoveStartOrAdvance' if starts else 'FighterMovePhysics'
    prefix='start' if starts else 'ground'
    calls={r['call']:r for r in rows if r['phase']==name+':entry' and
           (not starts or r['fighters'][(r['a0']-0x800A9228)//0x188C]['pending_move'])}
    paths={int(p.name.split('-')[1]):p for p in capture.glob(prefix+'-*-before.ranges')}
    if not calls or paths.keys()!=calls.keys():raise ValueError('missing/extra original physics snapshots')
    with tempfile.TemporaryDirectory() as tmp:
        listing,output=Path(tmp)/'pairs.tsv',Path(tmp)/'native.jsonl'
        lines=[]
        for call,row in sorted(calls.items()):
            player=(row['a0']-0x800A9228)//0x188C
            if player not in (0,1) or row['a0']!=0x800A9228+player*0x188C:
                raise ValueError('unexpected physics fighter pointer')
            before=paths[call].resolve();after=before.with_name(f'{prefix}-{call}-after.ranges')
            if not after.is_file():raise ValueError('missing physics return snapshot')
            lines.append(f'{before}\t{after}\t{player}\t{call}\n')
        listing.write_text(''.join(lines))
        subprocess.run([str(evaluator),str(tables/'psx_trig.bin'),str(tables/'psx_atan.bin'),
                        str(tables/'psx_push.bin'),str(provenance['step_accum_x']),
                        str(provenance['step_accum_z']),str(listing),str(output),
                        'start-ranges-v1' if starts else 'ranges-v1'],check=True)
        results=[json.loads(l) for l in output.read_text().splitlines()]
    if len(results)!=len(calls):raise ValueError('truncated native observations')
    coverage=Counter();failures=[]
    for result in results:
        coverage[str(result['slot'])]+=1
        if not result['passed']:failures.append(result)
    return dict(passed=not failures,cases=len(results),coverage=dict(coverage),failures=failures,
                trace_sha256=hashlib.sha256(trace.read_bytes()).hexdigest(),
                evaluator_sha256=hashlib.sha256(evaluator.read_bytes()).hexdigest(),
                scope=('Independently seeded ground move-start physical effects' if starts else
                       'Independently seeded FighterMovePhysics')+'; not complete fighter/root/collision or match parity')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--evaluator',type=Path,required=True);p.add_argument('--tables',type=Path,required=True)
    p.add_argument('--starts',action='store_true')
    a=p.parse_args();r=audit(a.capture.resolve(),a.evaluator.resolve(),a.tables.resolve(),a.starts)
    (a.capture/('ground-start-audit.json' if a.starts else 'ground-audit.json')).write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items() if k!='failures'},indent=2));print('mismatches:',len(r['failures']))
    raise SystemExit(0 if r['passed'] else 1)
