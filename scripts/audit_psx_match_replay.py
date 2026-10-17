#!/usr/bin/env python3
"""Compare one continuously advanced native match with original FightFrames.

Seeds once at the first captured entry; expected exit ranges only compare state.
A matching prefix/category is never presented as complete match parity.
"""
import argparse,collections,hashlib,json,struct,subprocess
from pathlib import Path
from analyze_psx_phases import analyze,verify_checkpoint
from psx_disc_import import US_EXE_SHA
from extract_psx_pose_tables import mapped
from prepare_psx_phase_capture import round_descriptors,pad_descriptors


def words(fighter):
    if fighter['held']&~15 or fighter['pressed']&~15:raise ValueError('unrepresentable original button bits')
    if fighter['direction']==5 and not fighter['held'] and not fighter['pressed']:
        return 0x8000,0 # Original InputClear, not an ordinary neutral pad sample.
    onehot=fighter['direction'];d=onehot.bit_length()-5
    if d not in range(1,10) or onehot!=1<<(d+4):raise ValueError('invalid original input direction')
    x=(d-1)%3-1;y=(d-1)//3-1
    held=(1 if x>0 else 2 if x<0 else 0)|(4 if y>0 else 8 if y<0 else 0)
    for b in range(4):
        if fighter['held']&(1<<b):held|=1<<(b+5)
    pressed=sum(1<<(b+5) for b in range(4) if fighter['pressed']&(1<<b))
    return held,pressed


def mapped_pad_words(held,pressed,side):
    if side not in (0,1) or not 0<=held<=65535 or not 0<=pressed<=65535:
        raise ValueError('invalid physical pad/side')
    value=0
    right,left=bool(held&0x2000),bool(held&0x8000)
    if right!=left:value|=1 if right!=(side==1) else 2
    up,down=bool(held&0x1000),bool(held&0x4000)
    if up!=down:value|=4 if up else 8
    edges=0
    for raw,port in ((0x80,32),(0x10,64),(0x40,128),(0x20,256)):
        if held&raw:value|=port
        if pressed&raw:edges|=port
    return value,edges


def frame_pad_words(entry,inputs,sides,replay_mode):
    # CpuOrPadInput refreshes mapped words during each InputSource call;
    # the early physical latch remains the RoundFlow victory-button source.
    if set(inputs)!={0,1} and replay_mode not in (1,2,3,4):
        raise ValueError('missing live InputSource pad sample')
    return tuple(v for i in range(2) for v in mapped_pad_words(
        inputs.get(i,entry)['pads']['held'+str(i)],
        inputs.get(i,entry)['pads']['pressed'+str(i)],sides[i]))


def audit(args):
    root=args.capture.resolve();meta=json.loads((root/'provenance.json').read_text())
    output=getattr(args,'output',None) or root
    output.mkdir(parents=True,exist_ok=True)
    if meta['exe_sha256']!=US_EXE_SHA or not meta.get('frame_replay_capture'):raise ValueError('unverified frame capture')
    original_flow=getattr(args,'original_round_flow',False)
    seed=(root/'frame-seed.bin').read_bytes()
    sides=[seed[0xA9228+i*0x188C+0xC6] for i in range(2)]
    if original_flow:
        layout_path=Path(__file__).resolve().parents[1]/'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json'
        layout_raw=layout_path.read_bytes();layout=json.loads(layout_raw)['blocks']['exe'];points=dict(layout['points'])
        if not meta.get('round_flow_capture') or meta['layout_sha256']!=hashlib.sha256(layout_raw).hexdigest() or \
            meta['round_fields']!=round_descriptors(lambda a:points.get(a) or mapped(layout,a,4),True) or \
            meta.get('pad_fields')!=pad_descriptors(lambda a:points.get(a) or mapped(layout,a,4)):
            raise ValueError('original round flow requires verified round/pad capture')
    observer=root/'observer.lua'
    if hashlib.sha256(observer.read_bytes()).hexdigest()!=meta['observer_sha256']:raise ValueError('observer hash mismatch')
    rows=[json.loads(line) for line in (root/'phases.jsonl').read_text().splitlines()];analyze(rows)
    if original_flow:
        if json.loads((root/'execution.json').read_text())['running'] or \
                json.loads((root/'paused-phase.jsonl').read_text())!=rows[-1]:
            raise ValueError('requires final paused observation')
        verify_checkpoint((root/'paused-ram.bin').read_bytes(),rows[-1],meta['round_fields'],meta['pad_fields'])
    files=sorted(root.glob('frame-*-after.ranges'),key=lambda p:int(p.name.split('-')[1]))
    if not files:raise ValueError('no continuous frame observations')
    first=int(files[0].name.split('-')[1]);expected={int(p.name.split('-')[1]):p for p in files}
    active=None;inputs={};cases=[];seed_entry=None;boundary=None
    for row in rows:
        if row['phase']=='FightFrame:entry':
            if row['call']==first:seed_entry=row
            active=row;inputs={}
        elif active and row['phase']=='InputSource:exit':
            index=(row['a0']-0x800A9228)//0x188C
            if index in (0,1):inputs[index]=row if original_flow else row['fighters'][index]
        elif row['phase']=='FightFrame:exit' and seed_entry is not None:
            if row['call'] not in expected:raise ValueError('missing consecutive original frame snapshot')
            if active is None or active['call']!=row['call'] or (not original_flow and set(inputs)!={0,1}):raise ValueError('incomplete frame nesting/inputs')
            try:
                if original_flow:
                    pair=frame_pad_words(active,inputs,sides,row['round']['replay_mode'])
                else:pair=(*words(inputs[0]),*words(inputs[1]))
            except ValueError as error:
                boundary=dict(call=row['call'],source_frame=row['source_frame'],
                              reason=str(error))
                break
            case=(expected[row['call']],*pair,row['call'],row['source_frame'])
            if original_flow:case+=tuple(active['round'][k] for k in ('physical0','physical1','pressed0','pressed1'))
            cases.append(case)
    if not cases or not seed_entry:raise ValueError('missing frame boundary')
    for i,f in enumerate(seed_entry['fighters']):
        b=0xA9228+i*0x188C
        if any(struct.unpack_from('<'+fmt,seed,b+off)[0]!=f[name] for name,off,fmt in
               [('slot',0xA0,'H'),('pose',0x58,'h'),('root_frame',0x50,'h'),('health',0x3F4,'i')]):
            raise ValueError('seed RAM differs from initial entry')
    listing=output/'replay-cases.tsv';listing.write_text(''.join('\t'.join(map(str,c))+'\n' for c in cases))
    args_list=[str(args.evaluator.resolve()),str(root/'frame-seed.bin'),str(listing),
               str(meta['step_accum_x']),str(meta['step_accum_z']),
               str(meta['body_addresses']['previous_root']),str(meta['body_addresses']['ramp'])]
    if original_flow:args_list.append('--original-round-flow')
    native_path=output/'native-frame-replay.jsonl'
    with native_path.open('w') as out:subprocess.run(args_list,stdout=out,check=True)
    native=[json.loads(line) for line in native_path.read_text().splitlines()]
    categories=collections.Counter();first_difference={};matching_prefix={};field_first={}
    for index,frame in enumerate(native):
        if frame['call']!=cases[index][5] or frame['native_tick']!=(index+1 if frame['supported'] else index):
            raise ValueError('native replay continuity failed')
        for difference in frame['differences']:
            category=difference['category'];categories[category]+=1
            field=str(difference['fighter'])+':'+category+':'+difference['field']
            field_first.setdefault(field,dict(tick=index+1,source_frame=frame['source_frame'],**difference))
            if category not in first_difference:
                first_difference[category]=dict(tick=index+1,source_frame=frame['source_frame'],differences=[d for d in frame['differences'] if d['category']==category])
                matching_prefix[category]=index
    checked_categories=['ground','move','hit','joint_origin','joint_rotation']
    if native and native[0].get('body_checked'):checked_categories+=['body']
    if native and native[0].get('blend_checked'):checked_categories+=['blend','blend_local']
    if original_flow:checked_categories+=['round','round_actor']
    committed=sum(bool(frame['supported']) for frame in native)
    for category in checked_categories:matching_prefix.setdefault(category,committed)
    report=dict(passed=not boundary and len(native)==len(cases) and all(r['passed'] for r in native),
                original_round_flow=original_flow,pad_initial_sides=sides if original_flow else None,
                frames_available=len(files),input_mode_boundary=boundary,
                frames_requested=len(cases),frames_attempted=len(native),frames_executed=committed,seed_count=1,
                clear_input_frames=[sum(bool(r['clear_inputs'][i]) for r in native if r['supported'])
                    if original_flow else sum(bool(c[1+2*i]&0x8000) for c in cases) for i in range(2)],
                unsupported=[index+1 for index,r in enumerate(native) if not r['supported']],
                differences_by_category=dict(categories),matching_prefix_frames=matching_prefix,
                first_difference=first_difference,first_difference_by_field=field_first,
                seed_sha256=hashlib.sha256(seed).hexdigest(),
                trace_sha256=hashlib.sha256((root/'phases.jsonl').read_bytes()).hexdigest(),
                evaluator_sha256=hashlib.sha256(args.evaluator.read_bytes()).hexdigest(),
                scope=('One original neutral entry seed, then uninterrupted native owned core with observed mapped pad words; '
                       'clear/script inputs are native, initial screen sides retained. ' if original_flow else
                       'One original neutral entry seed, then uninterrupted native owned core with observed inputs; ')+
                      'expected snapshots never reseed the simulation; full parity requires every category')
    (output/'frame-replay-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('first_difference','first_difference_by_field')},indent=2))
    for category,entry in first_difference.items():print(category,'first mismatch tick',entry['tick'],entry['differences'][:3])
    return report['passed']

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True);p.add_argument('--evaluator',type=Path,required=True)
    p.add_argument('--output',type=Path,help='separate audit directory to preserve earlier comparisons')
    p.add_argument('--original-round-flow',action='store_true',help='Run the original round controller with physical pad samples')
    raise SystemExit(not audit(p.parse_args()))
