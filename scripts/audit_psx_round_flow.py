#!/usr/bin/env python3
"""Audit normal VS RoundFlow against paired original instruction snapshots.

Each native call starts from the observed entry. This is a component audit,
not continuous match parity. Original data and generated reports stay private.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import subprocess

from analyze_psx_phases import analyze,verify_checkpoint
from audit_psx_round_score import Ranges,checked_state
from extract_psx_pose_tables import mapped
from prepare_psx_phase_capture import round_descriptors
from psx_disc_import import US_EXE_SHA
from psx_ram_oracle import FIGHTER_BASE,FIGHTER_STRIDE

GLOBALS=('state counter hold done replay_request frame played timer result input0 input1 '
         'freeze no_damage camera timer_stopped to_win max_rounds blend_hold human_mask '
         'tie_choice draw_wins match_result match_winner match_loser win_done0 win_done1').split()
ACTOR=('health maximum wins won win_pose active pose length bank_type state_word '
       'script_input is_cpu costume health_left carried_health').split()
FACTS='replay_mode replay_skip frame_counter physical0 physical1 pressed0 pressed1'.split()


def actor(ram,index,row,move_banks=None,flow=None):
    base=FIGHTER_BASE+index*FIGHTER_STRIDE
    values={name:ram.read(base+off,size,signed) for name,off,size,signed in [
        ('health',0x3F4,4,True),('maximum',0x3F8,4,True),('wins',0x44,2,True),
        ('won',0x46,2,True),('win_pose',0x48,2,True),('active',0xC3,1,False),
        ('pose',0x58,2,True),('bank_type',0x16,2,False),('script_input',0x408,2,False),
        ('is_cpu',0xC5,1,False),('costume',0x1C,2,True),('health_left',0x400,2,True),
        ('carried_health',0x3FC,4,True)]}
    pose=ram.read(base+0x54,4)
    pending=ram.read(base+0x1A4,4)
    values['length']=ram.read(pose+0x18,1)
    values['state_word']=ram.read((pending or pose)+4,4)
    if not pending and ram.read(pose+16,2)!=ram.read(base+0xA0,2):
        needed=flow and flow['state']==2 and values['health'] and values['won']>=0 and \
            ((flow['result']&1) or values['is_cpu']) and values['bank_type']!=18
        if move_banks is None or not needed:values['state_word']=None
        else:
            player=ram.read(base+0x12,2,True);stance=ram.read(pose+16,2,True)
            if player not in (0,1) or not 0<=stance<4023:raise ValueError('invalid stance lookup')
            bank=ram.read(move_banks+player*4,4);table=ram.read(bank+12,4)
            state_row=ram.read(table+stance*4,4)
            values['state_word']=ram.read(state_row+4,2)
    labels={'wins':'round_wins','won':'round_won','length':'pose_length'}
    for name in ('health','wins','won','win_pose','active','pose','length','health_left'):
        if row[labels.get(name,name)]!=values[name]:raise ValueError('fighter JSON/RAM disagreement: '+name)
    return values


def audit(capture,runner,layout_path):
    meta=json.loads((capture/'provenance.json').read_text())
    layout_raw=layout_path.read_bytes()
    if not meta.get('round_flow_capture') or meta['exe_sha256']!=US_EXE_SHA or \
            meta['layout_sha256']!=hashlib.sha256(layout_raw).hexdigest():
        raise ValueError('unverified round-flow capture executable/layout')
    if hashlib.sha256((capture/'observer.lua').read_bytes()).hexdigest()!=meta['observer_sha256']:
        raise ValueError('observer hash mismatch')
    layout=json.loads(layout_raw)['blocks']['exe'];points=dict(layout['points'])
    fields=round_descriptors(lambda a:points.get(a) or mapped(layout,a,4),True)
    if fields!=meta['round_fields']:raise ValueError('unverified round-flow descriptors')
    move_banks=meta.get('round_move_banks')
    if move_banks is not None and move_banks!=(points.get(0x800AE0E8) or mapped(layout,0x800AE0E8,4)):
        raise ValueError('unverified stance bank pointer table')
    trace=(capture/'phases.jsonl').read_bytes()
    rows=[json.loads(line) for line in trace.splitlines()];analyze(rows)
    if json.loads((capture/'execution.json').read_text())['running']:
        raise ValueError('capture must be paused')
    if json.loads((capture/'paused-phase.jsonl').read_text())!=rows[-1]:
        raise ValueError('paused observation differs from final trace row')
    verify_checkpoint((capture/'paused-ram.bin').read_bytes(),rows[-1],fields)
    entries={};cases=[];hashes={};edges=Counter();lines=[]
    for row in rows:
        if row['phase'] not in ('RoundFlow:entry','RoundFlow:exit'):continue
        edge='before' if row['phase'].endswith('entry') else 'after'
        name=f"round-{row['call']}-{edge}.ranges";raw=(capture/name).read_bytes()
        hashes[name]=hashlib.sha256(raw).hexdigest()
        state,_=checked_state(row,raw,fields)
        if state['frame_counter']!=row['source_frame']:raise ValueError('source frame disagrees with RAM')
        ram=Ranges(raw);pair=[actor(ram,i,row['fighters'][i],move_banks,state) for i in range(2)]
        if edge=='before':entries[row['call']]=(row,state,pair);continue
        before,old,pair_before=entries.pop(row['call'])
        for i,p in enumerate(pair_before):
            if p['state_word'] is None:
                needs_stance=old['state']==2 and p['health'] and p['won']>=0 and \
                    ((old['result']&1) or p['is_cpu']) and p['bank_type']!=18
                if needs_stance:raise ValueError('uncaptured stance row required for scripted input')
                p['state_word']=0
        values=[old[k] for k in GLOBALS]+[p[k] for p in pair_before for k in ACTOR]+[old[k] for k in FACTS]
        lines.append(' '.join(map(str,values))+'\n')
        cases.append((before,row,old,state,pair_before,pair));edges[old['state'],state['state']]+=1
    if entries or not cases:raise ValueError('missing paired RoundFlow observations')
    result=subprocess.run([str(runner)],input=''.join(lines),capture_output=True,text=True,check=True)
    native=[json.loads(line) for line in result.stdout.splitlines()]
    if len(native)!=len(cases):raise ValueError('native adapter did not execute every case')
    differences=[];transitions=[]
    for actual,(before,row,old,expected,pair_before,pair) in zip(native,cases):
        mismatch={}
        for k in GLOBALS:
            if actual['round'][k]!=expected[k]:mismatch['round.'+k]=[actual['round'][k],expected[k]]
        for k in FACTS:
            if old[k]!=expected[k]:mismatch['external.'+k]=[old[k],expected[k]]
        for i,(got,want) in enumerate(zip(actual['fighters'],pair)):
            for k in ('wins','won','win_pose','active','script_input','health_left','pose','carried_health'):
                if got[k]!=want[k]:mismatch[f'fighter{i}.{k}']=[got[k],want[k]]
            slot=got['start_slot'] if got['start_slot']>=0 else before['fighters'][i]['slot']
            if slot!=row['fighters'][i]['slot']:mismatch[f'fighter{i}.slot']=[slot,row['fighters'][i]['slot']]
        if mismatch:differences.append(dict(call=row['call'],source_frame=row['source_frame'],fields=mismatch))
        if old['state']!=expected['state']:
            transitions.append(dict(call=row['call'],source_frame=row['source_frame'],
                before=old,after=expected,start_slots=[p['start_slot'] for p in actual['fighters']]))
    return dict(format=1,scope='normal VS RoundFlow component; independently seeded entry per call, not continuous match parity',
        checked_round_fields=GLOBALS,checked_actor_fields=['wins','won','win_pose','active',
            'script_input','health_left','pose','carried_health','slot'],
        external_facts=FACTS,
        excluded='Move/ground/blend execution, replay playback, camera advancement, outer round reset, audio/effects and cumulative fight-clock storage',
        trace_sha256=hashlib.sha256(trace).hexdigest(),runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
        paused_ram_sha256=hashlib.sha256((capture/'paused-ram.bin').read_bytes()).hexdigest(),
        snapshot_sha256=hashes,paired_calls=len(cases),all_equal=not differences,
        edges={f'{a}->{b}':n for (a,b),n in sorted(edges.items())},
        differing_calls=len(differences),differences=differences,transitions=transitions)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--runner',type=Path,required=True)
    p.add_argument('--layout',type=Path,default=Path(__file__).resolve().parents[1]/'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();report=audit(a.capture,a.runner.resolve(),a.layout)
    a.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('snapshot_sha256','transitions','differences')},indent=2))
    if report['differences']:print(json.dumps(report['differences'][:5],indent=2))
    raise SystemExit(0 if report['all_equal'] else 1)
