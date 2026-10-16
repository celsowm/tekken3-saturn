#!/usr/bin/env python3
"""Audit original BodySeparate/ArenaBounds calls; not free-running match parity."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from analyze_psx_phases import analyze
from psx_disc_import import US_EXE_SHA


def audit(args):
    root=args.capture.resolve();meta=json.loads((root/'provenance.json').read_text())
    if meta['exe_sha256']!=US_EXE_SHA:raise ValueError('unverified original capture')
    rows=[json.loads(line) for line in (root/'phases.jsonl').read_text().splitlines()]
    analyze(rows)
    pairs={}
    for row in rows:
        name,edge=row['phase'].rsplit(':',1) if ':' in row['phase'] else ('','')
        if name in ('BodySeparate','ArenaBounds'):
            pairs.setdefault((name,row['call']),{})[edge]=row
    lines=[];coverage={}
    for (name,call),edges in pairs.items():
        if edges.keys()!={'entry','exit'}:raise ValueError('unpaired function call')
        kind='body' if name=='BodySeparate' else 'arena'
        index=(edges['entry']['a0']-0x800A9228)//0x188C
        if index not in (0,1):raise ValueError('invalid fighter pointer')
        paths=[root/f'{kind}-{call}-{edge}.ranges' for edge in ('before','after')]
        if any(not p.is_file() for p in paths):raise ValueError('missing CPU snapshot')
        lines.append('\t'.join([*(str(p) for p in paths),kind,str(index),str(call)]))
        coverage[name]=coverage.get(name,0)+1
    if not lines:raise ValueError('no original body/bounds calls')
    batch=root/'body-audit-input.tsv';batch.write_text('\n'.join(lines)+'\n')
    addresses=meta['body_addresses'];tables=args.tables.resolve()
    result=subprocess.check_output([str(args.evaluator.resolve()),str(tables/'psx_body.bin'),
          str(tables/'psx_trig.bin'),str(tables/'psx_atan.bin'),str(addresses['previous_root']),
          str(addresses['pair_distance']),str(addresses['ramp']),str(batch)],text=True)
    decoded=[json.loads(line) for line in result.splitlines()]
    if len(decoded)!=len(lines):raise ValueError('native runner omitted observations')
    (root/'body-audit-cases.jsonl').write_text(result)
    report=dict(passed=all(r['passed'] for r in decoded),cases=len(decoded),coverage=coverage,corrected_calls=sum(r['corrected'] for r in decoded),
                failures=[r for r in decoded if not r['passed']],
                evaluator_sha256=hashlib.sha256(args.evaluator.read_bytes()).hexdigest(),
                trace_sha256=hashlib.sha256((root/'phases.jsonl').read_bytes()).hexdigest(),
                scope='Independently seeded original normal-mode body/bounds; not match replay parity')
    (root/'body-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='failures'},indent=2))
    print('mismatches:',len(report['failures']))
    for failure in report['failures'][:3]:print(failure)
    return report['passed']


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--evaluator',type=Path,required=True)
    p.add_argument('--tables',type=Path,required=True)
    raise SystemExit(not audit(p.parse_args()))
