#!/usr/bin/env python3
"""Compare allocation-ordered semantic effect state in a complete retail fight.

Original free storage contains unrelated old RAM. Only fields initialized and
read by each type/phase are defined; copying its uninitialized bytes would be
state feedback. Type/flags/phase and object counts always compare first.
"""
import argparse
import json
import os
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from fight_oracle_eval import run


def defined_fields(o):
    kind,flags,state=o[:3];base=[0,1,2]
    if kind in (1,2,12,13):
        return base+[4,5,9,10,11] if not state else base+list(range(3,17))+(list(range(17,19)) if kind in (1,12) else [])
    if kind==3:
        return base+[15] if not state else base+[3,7,8,15,16]+([9,10,11] if flags&64 else [])
    if kind==0:return base+([15] if state==1 else [])
    if kind==17:return base+[15,16]+([17] if state else [])
    if kind==16:return base
    if kind in (4,5):
        return base+[9,10,11] if not state else base+[3,7,8,9,10,11,13,16 if kind==4 else 15]
    raise AssertionError(f'no semantic field policy for original effect type {kind}')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runner',type=Path,required=True);a=p.parse_args()
    case=ROOT/'build/parity-replay-sounds-retail-fight';original=ROOT/'build/parity-effects-pool-original.json'
    if not original.is_file() or not (case/'seed.json').is_file():
        print('private original effect-pool fixture absent; skipped');return 77
    audit=json.loads(original.read_text());meta=json.loads((case/'seed.json').read_text())
    if audit['original_exe_sha256']!=meta['original_exe_sha256'] or audit['remake_presentation_fixes'] is not False:
        raise AssertionError('original executable/restoration identity differs')
    os.environ['T3_EFFECT_POOL_TRACE']='1'
    os.environ['T3_EFFECT_ATOMIC_AUDIT']='1'
    rows,code=run(case,a.runner.resolve(),initialize_match=True,independent_camera=True)
    if 'effect transaction audit passed' not in (case/'native-camera.stderr').read_text():
        raise AssertionError('missing effect storage rollback proof')
    snaps=audit['snapshots']
    if code or len(rows)!=3000 or len(snaps)!=3000 or not all(r['passed'] for r in rows):
        raise AssertionError('complete core fight must pass before pool parity is considered')
    comparisons=objects=0;types=set()
    for fr,(snapshot,row) in enumerate(zip(snaps,rows),2):
        if snapshot['frame']!=fr or row['source_frame']!=fr:raise AssertionError('shifted/filtered frames')
        expected=[o['canonical'] for o in snapshot['objects']];actual=row['effects']
        if len(expected)!=len(actual):raise AssertionError(dict(frame=fr,expected_count=len(expected),actual_count=len(actual)))
        for i,(want,got) in enumerate(zip(expected,actual)):
            ds={k:[want[k],got[k]] for k in defined_fields(want) if want[k]!=got[k]}
            if ds:raise AssertionError(dict(frame=fr,object=i,differences=ds))
            comparisons+=len(defined_fields(want));objects+=1;types.add(want[0])
    print(json.dumps(dict(frames=len(rows),object_states_checked=objects,field_comparisons=comparisons,
                          original_types=sorted(types),core_and_rng_matched=True,
                          scope='Host semantic state, ordering and complete declared core fight; not pixels or native audio')))
    return 0

if __name__=='__main__':raise SystemExit(main())
