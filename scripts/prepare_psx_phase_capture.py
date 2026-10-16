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


def prepare(exe, layout_path, output, first, last, hit_ram=False,ground_ram=False,body_ram=False,contact_ram=False,frame_ram=False):
    raw = exe.read_bytes()
    layout = json.loads(layout_path.read_text())['blocks']['exe']
    if (hashlib.sha256(raw).hexdigest() != US_EXE_SHA or
            hashlib.sha256(raw[0x800:]).hexdigest() != layout['sha256']):
        raise ValueError('unsupported USA executable fingerprint/layout')
    if not 0 <= first < last <= 60000:
        raise ValueError('capture requires 0 <= first < last <= 60000')
    addresses = {name: mapped(layout, address, 4)
                 for name, address in FUNCTIONS.items()}
    if ground_ram:
        addresses['FighterMovePhysics']=mapped(layout,0x8003F330,4)
        addresses['MoveStartOrAdvance']=mapped(layout,0x8002F600,4)
    if body_ram:
        addresses['BodySeparate']=mapped(layout,0x8004401C,4)
        addresses['ArenaBounds']=mapped(layout,0x80043394,4)
    if contact_ram:addresses['CollisionShapesUpdate']=mapped(layout,0x80042204,4)
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
    a = p.parse_args()
    print(prepare(a.exe, a.layout, a.output, a.first, a.last,a.hit_ram,a.ground_ram,a.body_ram,a.contact_ram,a.frame_ram))
