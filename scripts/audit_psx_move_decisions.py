#!/usr/bin/env python3
"""Compare native phased input queues with real MoveStepAll observations.

Reports mismatches and unknown-fact limitations; never grants combat parity.
"""
import argparse
from collections import Counter,deque
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from analyze_psx_phases import analyze


def audit(trace,evaluator):
    rows=[json.loads(l) for l in trace.read_text().splitlines()];analyze(rows)
    histories=[deque(maxlen=60),deque(maxlen=60)];entries={};cases=[];skipped=Counter()
    for row in rows:
        if row['phase']=='MoveStepAll:entry':
            entries[row['call']]=row
            for i,f in enumerate(row['fighters']):
                direction=f['direction'].bit_length()-5
                if direction not in range(1,10) or f['direction']!=1<<(direction+4):
                    raise ValueError('not a one-hot original input direction')
                histories[i].append((direction,f['held'],f['pressed']))
        if row['phase']!='MoveStepAll:exit':continue
        entry=entries.pop(row['call'])
        for i,(a,b) in enumerate(zip(entry['fighters'],row['fighters'])):
            reason=('history_warmup' if len(histories[i])<60 else
                    'pending_on_entry' if a['pending_move'] else
                    'hit_reaction' if a['got_hit'] else None)
            if reason:skipped[reason]+=1;continue
            vals=(a['slot'],a['pose'],a['root_frame'],a['move_frame'],a['frame_step'],
                  a['freeze'],a['hold_frames'],a['ballistic'],a['move_changed'],a['entry_frame'],60)
            data=struct.pack('<11i',*vals)+bytes(v for h in histories[i] for v in h)
            cases.append((entry['source_frame'],entry['call'],i,a,b,data))
    if not cases:raise ValueError('no input-decision observations')
    with tempfile.TemporaryDirectory() as tmp:
        inp,out=Path(tmp)/'in',Path(tmp)/'out';inp.write_bytes(b''.join(c[-1] for c in cases))
        subprocess.run([str(evaluator),str(inp),str(out)],check=True)
        actual=list(struct.iter_unpack('<6i',out.read_bytes()))
    if len(actual)!=len(cases):raise ValueError('truncated decision output')
    mismatches=[];coverage=Counter()
    for case,got in zip(cases,actual):
        frame,call,i,before,after,_=case
        expected=(int(after['pending_move']!=0),after['pending_slot'] if after['pending_move'] else -1,
                  after['transition'],after['branch_kind'],after['entry_frame'],after['branch_window'])
        match=(got==expected) if expected[0] else (got[0]==0 and got[1]==-1)
        if not match:mismatches.append(dict(source_frame=frame,call=call,fighter=i,slot=before['slot'],
                                             native=got,original=expected))
        if expected[0]:coverage[str(expected[1])]+=1
    return dict(passed=not mismatches,cases=len(cases),mismatches=mismatches,
                original_queued_slots=dict(coverage),skipped=dict(skipped),
                trace_sha256=hashlib.sha256(trace.read_bytes()).hexdigest(),
                evaluator_sha256=hashlib.sha256(evaluator.read_bytes()).hexdigest(),
                scope='Each decision seeded independently with observed current move/clock/input history; '
                      'non-input reactions excluded, mutable branch facts not supplied; not free-running fight parity')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--trace',type=Path,required=True)
    p.add_argument('--evaluator',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();r=audit(a.trace,a.evaluator.resolve());a.output.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items() if k!='mismatches'},indent=2));print('mismatches:',len(r['mismatches']))
    raise SystemExit(0 if r['passed'] else 1)
