#!/usr/bin/env python3
"""Audit paired original CPU function observations without inventing tick timing."""
import argparse
from collections import Counter
import json
from pathlib import Path
from psx_ram_oracle import read_at, FIGHTER_BASE, FIGHTER_STRIDE, GLOBAL_FRAME, GAME_STATE, RAM_BYTES

# Independently decoded target layout; do not import Lua-generated values.
FIELDS = dict(slot=(0xA0,'h'), pose=(0x58,'h'), move_frame=(0x5C,'h'),
              health=(0x3F4,'i'), state=(0x60,'I'), damage=(0x5E,'h'),
              attack=(0x64,'H'), guard=(0x66,'H'), freeze=(0x76,'h'),
              got_hit=(0xCE,'B'), guarded=(0xCF,'B'), hit_clean=(0xD0,'B'),
              hit_done0=(0x83,'B'), hit_done1=(0x84,'B'), cooldown=(0x104,'h'),
              direction=(0x402,'H'), pressed=(0x404,'H'), held=(0x406,'H'),
              pending_slot=(0x1A0,'h'), root_x=(0,'i'), root_z=(8,'i'))
CLOCK_FIELDS = dict(root_frame=(0x50,'h'),frame_step=(0x6E,'h'),hold_frames=(0x6A,'h'),
                    ballistic=(0x92,'h'),move_changed=(0xDA,'B'),active=(0xC3,'B'),
                    pending_move=(0x1A4,'I'),entry_frame=(0x198,'h'),branch_window=(0x6C,'h'),
                    branch_kind=(0x8B,'B'),transition=(0x19A,'h'),current_transition=(0xA2,'h'),
                    hit_freeze_in=(0xEE,'h'),pose_move=(0x54,'I'),root_move=(0x4C,'I'))
CLOCK_FIELDS.update(round_wins=(0x44,'h'),round_won=(0x46,'h'),win_pose=(0x48,'h'),health_left=(0x400,'h'))


def read_fighter(ram, index, names):
    base=FIGHTER_BASE+index*FIGHTER_STRIDE
    mapping=dict(FIELDS,**CLOCK_FIELDS)
    result={name:read_at(ram,base+mapping[name][0],mapping[name][1])
            for name in names if name in mapping}
    if 'pose_hold_frame' in names:
        result['pose_hold_frame']=read_at(ram,read_at(ram,base+0x54,'I')+0x1B,'B')
    if 'root_is_pose' in names:
        result['root_is_pose']=int(read_at(ram,base+0x4C,'I')==read_at(ram,base+0x54,'I'))
    for name,offset,fmt,pointer in [('pose_length',0x18,'B',0x54),('root_length',0x18,'B',0x4C),
            ('pose_animation',0,'I',0x54),('pending_length',0x18,'B',0x1A4),
            ('pending_first',0x2D,'B',0x1A4),('pending_last',0x2E,'B',0x1A4),
            ('pending_animation',0,'I',0x1A4)]:
        if name in names:
            address=read_at(ram,base+pointer,'I')
            result[name]=read_at(ram,address+offset,fmt) if address else 0
    if result.keys()!=names:
        raise ValueError('unknown original fighter fields')
    return result


def verify_checkpoint(ram, last, round_fields=None):
    if len(ram) != RAM_BYTES or last['phase'] != 'FightFrame:exit':
        raise ValueError('requires full paused RAM and a final FightFrame return')
    if read_at(ram,GAME_STATE,'I') != 8 or read_at(ram,GLOBAL_FRAME,'I') != last['source_frame']:
        raise ValueError('paused game state/frame disagrees with checkpoint')
    pair = [read_fighter(ram,i,last['fighters'][i].keys()) for i in range(2)]
    if pair != last['fighters']:
        raise ValueError('paused RAM disagrees with final phase observation')
    if 'round' in last:
        if not round_fields or set(round_fields)!=set(last['round']):
            raise ValueError('round checkpoint requires matching provenance descriptors')
        for name,field in round_fields.items():
            if field['size'] not in (1,2,4) or type(field['signed']) is not bool:
                raise ValueError('invalid round field descriptor: '+name)
            fmt={(1,False):'B',(1,True):'b',(2,False):'H',(2,True):'h',(4,False):'I',(4,True):'i'}[(field['size'],field['signed'])]
            if read_at(ram,field['address'],fmt)!=last['round'][name]:
                raise ValueError('paused round RAM disagrees: '+name)
    return True


def analyze(rows):
    stack, calls, changes, vsync = [], set(), Counter(), Counter()
    frames, duplicates, last_frame, last_cycles = 0, 0, None, -1
    source_deltas = Counter()
    for seq, row in enumerate(rows):
        if row['seq'] != seq or row['cycles'] < last_cycles:
            raise ValueError('nonconsecutive observation sequence or decreasing CPU cycles')
        last_cycles = row['cycles']
        phase = row['phase']
        if len(row['fighters']) != 2:
            raise ValueError('observation must contain both fighters')
        if phase == 'GPU::Vsync':
            vsync[' > '.join(r['phase'].split(':')[0] for r in stack) or 'outside'] += 1
            continue
        name, edge = phase.split(':')
        if edge == 'entry':
            if row['call'] in calls:
                raise ValueError('reused function invocation ID')
            calls.add(row['call'])
            if name == 'FightFrame':
                if stack:
                    raise ValueError('nested FightFrame')
                if last_frame is not None:
                    delta = (row['source_frame'] - last_frame) & 0xffffffff
                    source_deltas[delta] += 1
                    duplicates += delta == 0
                last_frame = row['source_frame']
                frames += 1
            elif not stack:
                raise ValueError('subsystem outside FightFrame')
            stack.append(row)
        elif edge == 'exit':
            if not stack or stack[-1]['call'] != row['call'] or stack[-1]['phase'] != name+':entry':
                raise ValueError('unpaired or improperly nested function return')
            before = stack.pop()
            for player, (a, b) in enumerate(zip(before['fighters'], row['fighters'])):
                if a.keys() != b.keys():
                    raise ValueError('fighter observation fields changed')
                for field in a:
                    if a[field] != b[field]:
                        changes[name, player, field] += 1
        else:
            raise ValueError('unknown function edge')
    if stack:
        raise ValueError('capture ends inside an observed function')
    return dict(observations=len(rows), fight_calls=frames,
                repeated_source_counter_calls=duplicates,
                source_counter_deltas=dict(sorted(source_deltas.items())),
                vsync_locations=dict(vsync),
                changed_fields=[dict(function=name, fighter=player, field=field, changes=n)
                                for (name, player, field), n in sorted(changes.items())],
                interpretation='Observed entry/return changes; function-call ordinal, GPU Vsync and guest frame counter are separate clocks')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('trace', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--ram', type=Path, help='Optional paused 2 MiB RAM cross-check')
    p.add_argument('--provenance',type=Path,help='Round-global descriptors from the fingerprinted capture launcher')
    a = p.parse_args()
    rows = [json.loads(line) for line in a.trace.read_text().splitlines()]
    result = analyze(rows)
    if a.ram:
        fields=json.loads(a.provenance.read_text()).get('round_fields') if a.provenance else None
        result['paused_ram_equal'] = verify_checkpoint(a.ram.read_bytes(),rows[-1],fields)
    a.output.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k!='changed_fields'},indent=2))
