#!/usr/bin/env python3
"""Audit owned HitApply/reaction queue and immediate KO against private PS1 observations."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from analyze_psx_phases import analyze
from psx_ram_oracle import read_at,FIGHTER_BASE,FIGHTER_STRIDE
from psx_disc_import import US_EXE_SHA


def audit(args):
    root=args.capture.resolve();meta=json.loads((root/'provenance.json').read_text())
    if meta['exe_sha256']!=US_EXE_SHA:raise ValueError('unverified capture')
    rows=[json.loads(line) for line in (root/'phases.jsonl').read_text().splitlines()]
    analyze(rows);calls={};steps={};fight=None
    for row in rows:
        if row['phase']=='FightFrame:entry':fight=row['call']
        if row['phase']=='HitApply:entry':calls[row['call']]=(fight,row)
        if row['phase']=='MoveStepAll:exit':steps[fight]=row
    results=[];unavailable=set()
    for before in sorted(root.glob('hit-*-before.bin'),key=lambda p:int(p.name.split('-')[1])):
        call=int(before.name.split('-')[1]);fight,entry=calls[call]
        defender=(entry['a0']-FIGHTER_BASE)//FIGHTER_STRIDE
        if defender not in (0,1):raise ValueError('invalid defender')
        b=FIGHTER_BASE+defender*FIGHTER_STRIDE
        after=before.with_name(f'hit-{call}-after.bin').read_bytes()
        native=json.loads(subprocess.check_output([str(args.evaluator.resolve()),str(before),str(defender),
             str(args.bank.resolve()),str(args.common.resolve()),str(args.tables.resolve()/'psx_trig.bin'),
             str(args.tables.resolve()/'psx_atan.bin'),str(args.tables.resolve()/'psx_push.bin'),str(args.reactions.resolve())]))
        checks={}
        for name,offset,fmt in [('health_q16',0x3F4,'i'),('last_damage',0xE8,'h'),
                              ('guarded',0xCF,'B'),('hit_clean',0xD0,'B'),('counter',0xD3,'B')]:
            checks[name]=native[name]==read_at(after,b+offset,fmt)
        source=steps[fight]['fighters'][defender]
        for name,field in [('slot','slot'),('pose','pose'),('root_frame','root_frame'),
                           ('move_frame','move_frame'),('freeze','freeze'),('pending_slot','pending_slot'),
                           ('entry_frame','entry_frame'),('branch_kind','branch_kind')]:
            if field in source:checks[name]=native[name]==source[field]
            else:unavailable.add(field)
        field='transition' if native['pending_slot']>=0 else 'current_transition'
        if field in source:checks['transition']=native['transition']==source[field]
        else:unavailable.add(field)
        results.append(dict(call=call,source_frame=entry['source_frame'],defender=defender,
                            native=native,source=source,checks=checks,passed=all(checks.values())))
    if not results:raise ValueError('no original hit observations')
    report=dict(passed=all(r['passed'] for r in results),cases=len(results),unavailable_fields=sorted(unavailable),
                ko_cases=sum(r['native']['ko_started'] for r in results),results=results,
                evaluator_sha256=hashlib.sha256(args.evaluator.read_bytes()).hexdigest(),
                bank_sha256=hashlib.sha256(args.bank.read_bytes()).hexdigest(),
                common_sha256=hashlib.sha256(args.common.read_bytes()).hexdigest(),
                reactions_sha256=hashlib.sha256(args.reactions.read_bytes()).hexdigest(),
                trace_sha256=hashlib.sha256((root/'phases.jsonl').read_bytes()).hexdigest(),
                scope='Seeded owned hit transaction through MoveStepAll; not full free-running match parity')
    (root/'owned-hit-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='results'},indent=2))
    for result in results:
        if not result['passed']:print('mismatch',result['call'],[k for k,v in result['checks'].items() if not v])
    return report['passed']


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('capture','evaluator','bank','common','tables','reactions'):
        p.add_argument('--'+name,type=Path,required=True)
    raise SystemExit(not audit(p.parse_args()))
