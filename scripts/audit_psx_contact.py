#!/usr/bin/env python3
"""Audit original CollisionShapesUpdate/HitTest calls; not free-running match parity."""
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
        if name in ('CollisionShapesUpdate','HitTest'):
            pairs.setdefault((name,row['call']),{})[edge]=row
    lines=[];coverage={}
    for (name,call),edges in pairs.items():
        if edges.keys()!={'entry','exit'}:raise ValueError('unpaired function call')
        kind='shape' if name=='CollisionShapesUpdate' else 'contact'
        index=(edges['entry']['a0']-0x800A9228)//0x188C
        if index not in (0,1):raise ValueError('invalid fighter pointer')
        paths=[root/f'{kind}-{call}-{edge}.ranges' for edge in ('before','after')]
        if any(not p.is_file() for p in paths):raise ValueError('missing CPU snapshot')
        lines.append('\t'.join([*(str(p) for p in paths),kind,str(index),str(call)]))
        coverage[name]=coverage.get(name,0)+1
    if not lines:raise ValueError('no original shapes/contact calls')
    batch=root/'contact-audit-input.tsv';batch.write_text('\n'.join(lines)+'\n')
    addresses=meta['segment_addresses'];tables=args.tables.resolve()
    result=subprocess.check_output([str(args.evaluator.resolve()),str(tables/'psx_trig.bin'),
          str(addresses['anchor']),str(addresses['end0']),str(addresses['end1']),
          str(addresses['mode']),str(batch)],text=True)
    decoded=[json.loads(line) for line in result.splitlines()]
    if len(decoded)!=len(lines):raise ValueError('native runner omitted observations')
    (root/'contact-audit-cases.jsonl').write_text(result)
    report=dict(passed=all(r['passed'] for r in decoded),cases=len(decoded),coverage=coverage,accepted_contacts=sum(r['accepted'] for r in decoded),
                failures=[r for r in decoded if not r['passed']],
                evaluator_sha256=hashlib.sha256(args.evaluator.read_bytes()).hexdigest(),
                trace_sha256=hashlib.sha256((root/'phases.jsonl').read_bytes()).hexdigest(),
                scope='Independently seeded original world shapes/HitTest; not match replay parity')
    (root/'contact-audit.json').write_text(json.dumps(report,indent=2)+'\n')
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
