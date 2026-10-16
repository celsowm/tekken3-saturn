#!/usr/bin/env python3
"""Compare native VM clock advancement with original MoveStartAll boundaries.

Pending move starts are counted as unsupported, never misreported as passes.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile

from analyze_psx_phases import analyze

ORDER=('pose','root_frame','move_frame','frame_step','freeze','hold_frames','ballistic','move_changed')
RECORD=struct.Struct('<9i')


def audit(trace,evaluator,pending_starts=False):
    rows=[json.loads(l) for l in trace.read_text().splitlines()]
    analyze(rows)
    pending={};cases=[];coverage=Counter();skipped=Counter()
    for row in rows:
        if row['phase']=='MoveStartAll:entry':pending[row['call']]=row
        if row['phase']!='MoveStartAll:exit':continue
        before=pending.pop(row['call'])
        for player,(a,b) in enumerate(zip(before['fighters'],row['fighters'])):
            if not a['active']:skipped['inactive']+=1;continue
            if bool(a['pending_move'])!=pending_starts:
                skipped['no_pending_move' if pending_starts else 'pending_move_start']+=1;continue
            vals=tuple(a[k] for k in ORDER)
            if pending_starts:
                vals+=(a['entry_frame'],a['branch_window'],a['hit_freeze_in'],
                       a['transition'],a['branch_kind'],a['pose_length'],a['pending_length'],
                       a['root_length'],a['pending_first'],a['pending_last'],
                       int(a['pose_animation']==a['pending_animation']),a['got_hit'],
                       int(a['health']!=0),a['pose_hold_frame'])
            else:vals+=(a['pose_hold_frame'],)
            cases.append((row['call'],player,vals,b,a))
            key=('freeze' if a['freeze']>0 else 'ballistic' if a['ballistic'] else
                 'hold' if a['hold_frames']>=0 else 'reverse' if a['frame_step']<0 else 'forward')
            coverage[key]+=1
            if pending_starts:coverage['transition_%02x'%a['transition']]+=1
    if not cases:raise ValueError('no original no-pending-move observations')
    with tempfile.TemporaryDirectory() as tmp:
        source,dest=Path(tmp)/'input.bin',Path(tmp)/'output.bin'
        inp=struct.Struct('<22i') if pending_starts else RECORD
        out=struct.Struct('<10i') if pending_starts else RECORD
        source.write_bytes(b''.join(inp.pack(*v) for _,_,v,_,_ in cases))
        subprocess.run([str(evaluator)]+(['--start'] if pending_starts else [])+
                       [str(source),str(dest)],check=True)
        output=dest.read_bytes()
    if len(output)!=len(cases)*out.size:raise ValueError('incomplete native clock output')
    mismatches=[]
    for i,(call,player,vals,after,before) in enumerate(cases):
        actual=out.unpack_from(output,i*out.size)
        fields=[k for k,n in zip(ORDER,actual[:8]) if after[k]!=n]
        if actual[8] and not after['root_is_pose']:fields.append('rootMove binding')
        if pending_starts:
            if bool(actual[9])!=(after['pending_move']==0):fields.append('pending start decision')
            if actual[9] and after['slot']!=before['pending_slot']:fields.append('started slot')
            if actual[9] and after['current_transition']!=before['transition']:fields.append('transition')
        if fields:mismatches.append(dict(call=call,fighter=player,fields=fields,before=vals,
                                         native=actual,original=after))
    return dict(passed=not mismatches,cases=len(cases),coverage=dict(coverage),
                skipped=dict(skipped),mismatches=mismatches,
                trace_sha256=hashlib.sha256(trace.read_bytes()).hexdigest(),
                evaluator_sha256=hashlib.sha256(evaluator.read_bytes()).hexdigest(),
                scope=('Ground move-start clock/entry test' if pending_starts else 'No-pending clock advance')+
                      '; not input-branch selection or fighter physics parity')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--trace',type=Path,required=True)
    p.add_argument('--evaluator',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--pending',action='store_true')
    a=p.parse_args();r=audit(a.trace,a.evaluator.resolve(),a.pending)
    a.output.write_text(json.dumps(r,indent=2)+'\n')
    print(json.dumps({k:v for k,v in r.items() if k!='mismatches'},indent=2))
    raise SystemExit(0 if r['passed'] else 1)
