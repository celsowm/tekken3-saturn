#!/usr/bin/env python3
"""Prepare a private, fingerprint-checked instruction-phase capture launcher.

Only emits address metadata and local paths, never executable/game bytes.
Run the emitted Lua with PCSX-Redux -interpreter -debugger -dofile.
"""
import argparse
import hashlib
import json
from pathlib import Path

from extract_psx_pose_tables import mapped
from psx_disc_import import US_EXE_SHA

FUNCTIONS = dict(FightFrame=0x8002B0AC, MoveStartAll=0x80045834,
                 HitTest=0x80044304, HitApply=0x80044634,
                 InputSource=0x8002BF3C, MoveStepAll=0x80045450)
ROUND_FIELDS = {
    'state':(0x80097350,4,True),'counter':(0x80097354,4,True),
    'hold':(0x8009735C,4,True),'done':(0x80097360,4,True),
    'replay_request':(0x80097358,4,True),'frame':(0x80095884,4,True),
    'played':(0x800958A8,4,True),'timer':(0x800AE094,4,True),
    'result':(0x800AE340,2,False),'input0':(0x80095894,4,True),
    'input1':(0x80095898,4,True),'freeze':(0x8009588C,4,True),
    'no_damage':(0x800958A0,4,True),'camera':(0x80095890,4,True),
    'timer_stopped':(0x800958D4,4,True),'to_win':(0x800AE2C4,2,False),
    'max_rounds':(0x800AE404,2,False),'blend_hold':(0x800A9234,4,True),
    'human_mask':(0x800AE3D8,2,False),'tie_choice':(0x800AE406,1,False),
    'draw_wins':(0x800AFF58,1,False),'match_result':(0x800AFF71,1,False),
    'match_winner':(0x800AFF72,1,False),'match_loser':(0x800AFF73,1,False),
}


def round_descriptors(point):
    fields={name:dict(address=point(a),size=n,signed=s) for name,(a,n,s) in ROUND_FIELDS.items()}
    # USA MatchResult (0x8003E800) stores bytes at base+0x21/22/23.
    # The pinned layout's isolated winner/loser points conflict with this struct.
    base=point(0x800AFF50)
    for name,offset in [('match_result',0x21),('match_winner',0x22),('match_loser',0x23)]:
        fields[name]['address']=base+offset
    return fields


def prepare(exe, layout_path, output, first, last, hit_ram=False,ground_ram=False,body_ram=False,contact_ram=False,frame_ram=False,round_ram=False,stop_round_results=0):
    raw = exe.read_bytes()
    layout = json.loads(layout_path.read_text())['blocks']['exe']
    if (hashlib.sha256(raw).hexdigest() != US_EXE_SHA or
            hashlib.sha256(raw[0x800:]).hexdigest() != layout['sha256']):
        raise ValueError('unsupported USA executable fingerprint/layout')
    if not 0 <= first < last <= 60000:
        raise ValueError('capture requires 0 <= first < last <= 60000')
    if not 0<=stop_round_results<=9 or (stop_round_results and not round_ram):
        raise ValueError('round result stop requires round capture and count 1..9')
    addresses = {name: mapped(layout, address, 4)
                 for name, address in FUNCTIONS.items()}
    if ground_ram:
        addresses['FighterMovePhysics']=mapped(layout,0x8003F330,4)
        addresses['MoveStartOrAdvance']=mapped(layout,0x8002F600,4)
    if body_ram:
        addresses['BodySeparate']=mapped(layout,0x8004401C,4)
        addresses['ArenaBounds']=mapped(layout,0x80043394,4)
    if contact_ram:addresses['CollisionShapesUpdate']=mapped(layout,0x80042204,4)
    if round_ram:
        addresses.update({name:mapped(layout,a,4) for name,a in
            [('RoundFlow',0x8003CDD8),('RoundWins',0x8003E7C4),('WinPoseStart',0x8002D6B0)]})
    points=dict(layout['points'])
    def point(address):return points.get(address) or mapped(layout,address,4)
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    script = Path(__file__).resolve().with_name('pcsx_redux_phase_capture.lua')
    config = ('T3_PHASE_OPTIONS={first=%d,last=%d,output=%s,ram_prefix=%s,ground_prefix=%s,start_prefix=%s,'
              'step_x=%d,step_z=%d,' %
              (first, last, json.dumps(str(output / 'phases.jsonl')),
               json.dumps(str(output / 'hit-')) if hit_ram else 'nil',
               json.dumps(str(output / 'ground-')) if ground_ram else 'nil',
               json.dumps(str(output / 'start-')) if ground_ram else 'nil',
               point(0x8009E9A8),point(0x8009E9B8)))
    extra_ranges=[]
    round_fields={}
    if round_ram:
        round_fields=round_descriptors(point)
        config+='stop_round_results=%d,round_prefix=%s,round_fields={' % (stop_round_results,json.dumps(str(output/'round-')))
        config+=','.join('{%s,%d,%d,%s}'%(json.dumps(name),f['address'],f['size'],str(f['signed']).lower()) for name,f in round_fields.items())+'},'
        canonical_spans=[(0x80097350,20),(0x80095884,0x54),
            (0x800AE090,12),(0x800AE330,0x20),(0x800AE2C4,4),(0x800AE404,4),
            (0x800A9234,4),(0x800AE3D8,4),(0x800AFF50,0x24)]
        spans=[(point(a),n) for a,n in canonical_spans]
        if any(not any(a<=f['address'] and f['address']+f['size']<=a+n for a,n in spans) for f in round_fields.values()):
            raise ValueError('round field mapping lies outside captured ranges')
        extra_ranges+=canonical_spans
    if frame_ram:
        config += 'frame_prefix=%s,frame_seed=%s,' % (
            json.dumps(str(output/'frame-')),json.dumps(str(output/'frame-seed.bin')))
        extra_ranges += [(0x8009EAA0,32),(0x8009EA08,64),(0x80097E68,24)]
    if body_ram:
        config += 'body_prefix=%s,arena_prefix=%s,' % (
            json.dumps(str(output/'body-')),json.dumps(str(output/'arena-')))
        extra_ranges += [(0x8009EAA0,32),(0x8009EA08,64),(0x80097E68,24)]
    if contact_ram:
        config += 'contact_prefix=%s,shape_prefix=%s,' % (
            json.dumps(str(output/'contact-')),json.dumps(str(output/'shape-')))
        extra_ranges += [(0x800A0920,24),(0x800A08D0,24),(0x800A08F4,24),(0x800A0950,8),(0x800A0960,32)]
    if extra_ranges:
        config+='extra_ranges={'+','.join('{%d,%d}' % (point(a),n) for a,n in extra_ranges)+'},'
    config+='functions={'
    config += ','.join('%s=%d' % pair for pair in addresses.items()) + '}}\n'
    config += 'assert(loadfile(%s))()\n' % json.dumps(str(script))
    launcher = output / 'capture.lua'
    launcher.write_text(config)
    manifest = dict(exe_sha256=US_EXE_SHA, first_source_frame=first,
                    last_source_frame=last, functions=addresses,
                    step_accum_x=point(0x8009E9A8),step_accum_z=point(0x8009E9B8),
                    layout_sha256=hashlib.sha256(layout_path.read_bytes()).hexdigest(),
                    observer_sha256=hashlib.sha256(script.read_bytes()).hexdigest(),
                    observer='read-only Exec breakpoints at entry and saved return PC')
    if body_ram or frame_ram:
        manifest['body_addresses']={name:point(address) for name,address in
            [('previous_root',0x8009EAA0),('pair_distance',0x8009EA08),('ramp',0x80097E68)]}
    if round_ram:manifest.update(round_fields=round_fields,stop_round_results=stop_round_results,
        match_field_basis='USA MatchResult 0x8003E800: mapped AFF50 struct + offsets 0x21/0x22/0x23; isolated winner/loser layout points overridden')
    if contact_ram:
        manifest['segment_addresses']={name:point(address) for name,address in
            [('anchor',0x800A0920),('end0',0x800A08D0),('end1',0x800A08F4),('mode',0x800A0950),('trig',0x800A0960)]}
    manifest['frame_replay_capture']=frame_ram
    (output / 'provenance.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return launcher


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, required=True)
    p.add_argument('--layout', type=Path, default=Path(__file__).resolve().parents[1] /
                   'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--first', type=int, default=650)
    p.add_argument('--last', type=int, default=1700)
    p.add_argument('--hit-ram', action='store_true', help='Save pre/post HitApply RAM on actual gotHit')
    p.add_argument('--ground-ram',action='store_true',help='Read-only compact RAM ranges around FighterMovePhysics')
    p.add_argument('--body-ram',action='store_true',help='Capture normal body separation and arena bounds')
    p.add_argument('--contact-ram',action='store_true',help='Capture original world shapes and HitTest gates')
    p.add_argument('--frame-ram',action='store_true',help='One neutral full-RAM seed and consecutive FightFrame exit ranges')
    p.add_argument('--round-ram',action='store_true',help='Read-only round globals and RoundFlow/result/win-pose phases')
    p.add_argument('--stop-round-results',type=int,default=0,help='Pause at the FightFrame return after this many observed RoundWins calls')
    a = p.parse_args()
    print(prepare(a.exe, a.layout, a.output, a.first, a.last,a.hit_ram,a.ground_ram,a.body_ram,a.contact_ram,a.frame_ram,a.round_ram,a.stop_round_results))
