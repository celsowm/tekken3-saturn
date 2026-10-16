#!/usr/bin/env python3
"""Compare the portable C HitApply against original pre/post CPU observations.

This proves arithmetic/classification for observed contacts, not live Saturn
collision, command timing, physics or complete fight parity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from psx_disc_import import US_EXE_SHA
from psx_ram_oracle import read_at,FIGHTER_BASE,FIGHTER_STRIDE
from analyze_psx_phases import analyze,read_fighter


def audit(directory,evaluator,exe):
    if hashlib.sha256(exe.read_bytes()).hexdigest()!=US_EXE_SHA:
        raise ValueError('unsupported original executable')
    provenance=json.loads((directory/'provenance.json').read_text())
    if provenance['exe_sha256']!=US_EXE_SHA:
        raise ValueError('capture provenance disagrees with executable')
    rows=[json.loads(l) for l in (directory/'phases.jsonl').read_text().splitlines()]
    analyze(rows) # require paired, correctly nested observations
    by_call={}
    fight=None
    steps={}
    for row in rows:
        if row['phase']=='FightFrame:entry':fight=row['call']
        elif row['phase']=='MoveStepAll:exit':steps[fight]=row
        if row['phase'].startswith('HitApply:'):
            by_call.setdefault(row['call'],{})[row['phase'].split(':')[1]]=(fight,row)
    results=[]
    for before_file in sorted(directory.glob('hit-*-before.bin'),
                              key=lambda p:int(p.name.split('-')[1])):
        call=int(before_file.name.split('-')[1])
        _,entry=by_call[call]['entry']
        fight,exit_row=by_call[call]['exit']
        defender=(entry['a0']-FIGHTER_BASE)//FIGHTER_STRIDE
        if defender not in (0,1) or FIGHTER_BASE+defender*FIGHTER_STRIDE!=entry['a0']:
            raise ValueError('invalid HitApply fighter pointer')
        before=before_file.read_bytes()
        after=before_file.with_name(f'hit-{call}-after.bin').read_bytes()
        for ram,row in [(before,entry),(after,exit_row)]:
            pair=[read_fighter(ram,i,row['fighters'][i].keys()) for i in range(2)]
            if pair!=row['fighters']:
                raise ValueError('hit RAM does not match phase observation')
        base=entry['a0']
        # HitApply recomputes these from both fighters before classification.
        # Classification uses only <=0x3FFF; reaction selection uses quadrant.
        # Require those predicates to be invariant across the game's relative
        # angle refresh. The adapter still consumes exclusively PRE-hit facts.
        # This does not establish parity of the angle arithmetic itself.
        angle_before=read_at(before,base+0x30,'H')
        angle_after=read_at(after,base+0x30,'H')
        if ((angle_before<=0x3FFF)!=(angle_after<=0x3FFF) or
            read_at(before,base+0x32,'H')!=read_at(after,base+0x32,'H')):
            raise ValueError('classification angle predicates changed; native angle refresh required')
        actual=json.loads(subprocess.check_output([str(evaluator),str(before_file),str(defender)]))
        checks={}
        for name,off,fmt in [('health_q16',0x3F4,'i'),('guarded',0xCF,'B'),
                             ('hit_clean',0xD0,'B'),('counter',0xD3,'B')]:
            checks[name]=actual[name]==read_at(after,base+off,fmt)
        checks['ko_started']=actual['ko_started']==int(read_at(before,base+0xDE,'B')==0 and
                                                       read_at(after,base+0xDE,'B')!=0)
        for i,slot in enumerate(actual['slots']):
            address=base+0x13C+i*0x2C
            for name,off,fmt in [('used',0x21,'B'),('damage',0x1E,'h'),
                                 ('base_damage',0x1C,'h'),('no_damage',0x28,'B'),
                                 ('guarded',0x22,'B'),('chip',0x23,'B'),('close',0x26,'B'),
                                 ('hit',0x24,'B'),('counter',0x25,'B'),('airborne',0x27,'B')]:
                if name=='used' or slot['used']:
                    checks[f'slot{i}.{name}']=slot[name]==read_at(after,address+off,fmt)
        # The pure selector yields the normal reaction. MoveStepAll overrides
        # that with immediate FighterCollapse on KO; the owned transaction
        # audit verifies that later phase, not this arithmetic-only helper.
        if actual['reaction_known'] and not actual['ko_started']:
            checks['reaction_slot']=actual['reaction_slot']==steps[fight]['fighters'][defender]['pending_slot']
        results.append(dict(call=call,source_frame=entry['source_frame'],defender=defender,
                            attacker_slot=entry['fighters'][1-defender]['slot'],
                            native=actual,checks=checks,passed=all(checks.values()),
                            relative_angle_before=angle_before,relative_angle_after=angle_after,
                            before_sha256=hashlib.sha256(before).hexdigest(),
                            after_sha256=hashlib.sha256(after).hexdigest()))
    if not results:
        raise ValueError('no observed HitApply RAM pairs')
    expected={call for call,edges in by_call.items()
              if any(f['got_hit'] for f in edges['entry'][1]['fighters']) and
              edges['entry'][1]['fighters'][(edges['entry'][1]['a0']-FIGHTER_BASE)//FIGHTER_STRIDE]['got_hit']}
    if expected!={r['call'] for r in results}:
        raise ValueError('missing or extra per-hit RAM pairs')
    return dict(passed=all(r['passed'] for r in results),hits=len(results),results=results,
                evaluator_sha256=hashlib.sha256(evaluator.read_bytes()).hexdigest(),
                trace_sha256=hashlib.sha256((directory/'phases.jsonl').read_bytes()).hexdigest(),
                live_saturn_combat_parity=False,
                ko_reaction_queue_unavailable=sum(r['native']['ko_started'] for r in results),
                reaction_queue_cases=sum('reaction_slot' in r['checks'] for r in results))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--evaluator',type=Path,required=True)
    p.add_argument('--exe',type=Path,required=True)
    a=p.parse_args()
    result=audit(a.capture.resolve(),a.evaluator.resolve(),a.exe.resolve())
    (a.capture/'native-hit-audit.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='results'},indent=2))
    raise SystemExit(0 if result['passed'] else 1)
