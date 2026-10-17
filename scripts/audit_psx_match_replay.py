#!/usr/bin/env python3
"""Compare one continuously advanced native match with original FightFrames.

Seeds once at the first captured entry; expected exit ranges only compare state.
A matching prefix/category is never presented as complete match parity.
"""
import argparse,collections,hashlib,json,struct,subprocess
from pathlib import Path
from analyze_psx_phases import analyze
from psx_disc_import import US_EXE_SHA


def words(fighter):
    if fighter['held']&~15 or fighter['pressed']&~15:raise ValueError('unrepresentable original button bits')
    onehot=fighter['direction'];d=onehot.bit_length()-5
    if d not in range(1,10) or onehot!=1<<(d+4):raise ValueError('invalid original input direction')
    x=(d-1)%3-1;y=(d-1)//3-1
    held=(1 if x>0 else 2 if x<0 else 0)|(4 if y>0 else 8 if y<0 else 0)
    for b in range(4):
        if fighter['held']&(1<<b):held|=1<<(b+5)
    pressed=sum(1<<(b+5) for b in range(4) if fighter['pressed']&(1<<b))
    return held,pressed


def audit(args):
    root=args.capture.resolve();meta=json.loads((root/'provenance.json').read_text())
    output=getattr(args,'output',None) or root
    output.mkdir(parents=True,exist_ok=True)
    if meta['exe_sha256']!=US_EXE_SHA or not meta.get('frame_replay_capture'):raise ValueError('unverified frame capture')
    observer=root/'observer.lua'
    if hashlib.sha256(observer.read_bytes()).hexdigest()!=meta['observer_sha256']:raise ValueError('observer hash mismatch')
    rows=[json.loads(line) for line in (root/'phases.jsonl').read_text().splitlines()];analyze(rows)
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
            if index in (0,1):inputs[index]=row['fighters'][index]
        elif row['phase']=='FightFrame:exit' and seed_entry is not None:
            if row['call'] not in expected:raise ValueError('missing consecutive original frame snapshot')
            if active is None or active['call']!=row['call'] or set(inputs)!={0,1}:raise ValueError('incomplete frame nesting/inputs')
            try:pair=(*words(inputs[0]),*words(inputs[1]))
            except ValueError:
                boundary=dict(call=row['call'],source_frame=row['source_frame'],
                              reason='Original InputSource emitted a clear/scripted non-one-hot direction; owned pad adapter lacks that input mode')
                break
            cases.append((expected[row['call']],*pair,row['call'],row['source_frame']))
    if not cases or not seed_entry:raise ValueError('missing frame boundary')
    seed=(root/'frame-seed.bin').read_bytes()
    for i,f in enumerate(seed_entry['fighters']):
        b=0xA9228+i*0x188C
        if any(struct.unpack_from('<'+fmt,seed,b+off)[0]!=f[name] for name,off,fmt in
               [('slot',0xA0,'H'),('pose',0x58,'h'),('root_frame',0x50,'h'),('health',0x3F4,'i')]):
            raise ValueError('seed RAM differs from initial entry')
    listing=output/'replay-cases.tsv';listing.write_text(''.join('\t'.join(map(str,c))+'\n' for c in cases))
    args_list=[str(args.evaluator.resolve()),str(root/'frame-seed.bin'),str(listing),
               str(meta['step_accum_x']),str(meta['step_accum_z']),
               str(meta['body_addresses']['previous_root']),str(meta['body_addresses']['ramp'])]
    native_path=output/'native-frame-replay.jsonl'
    with native_path.open('w') as out:subprocess.run(args_list,stdout=out,check=True)
    native=[json.loads(line) for line in native_path.read_text().splitlines()]
    categories=collections.Counter();first_difference={};matching_prefix={};field_first={}
    for index,frame in enumerate(native):
        if frame['call']!=cases[index][-2] or frame['native_tick']!=index+1:raise ValueError('native replay continuity failed')
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
    for category in checked_categories:matching_prefix.setdefault(category,len(native))
    report=dict(passed=not boundary and len(native)==len(cases) and all(r['passed'] for r in native),
                frames_available=len(files),input_mode_boundary=boundary,
                frames_requested=len(cases),frames_executed=len(native),seed_count=1,
                unsupported=[r['native_tick'] for r in native if not r['supported']],
                differences_by_category=dict(categories),matching_prefix_frames=matching_prefix,
                first_difference=first_difference,first_difference_by_field=field_first,
                seed_sha256=hashlib.sha256(seed).hexdigest(),
                trace_sha256=hashlib.sha256((root/'phases.jsonl').read_bytes()).hexdigest(),
                evaluator_sha256=hashlib.sha256(args.evaluator.read_bytes()).hexdigest(),
                scope='One original neutral entry seed, then uninterrupted native owned core with observed inputs; '
                      'expected snapshots never reseed the simulation; full parity requires every category')
    (output/'frame-replay-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ('first_difference','first_difference_by_field')},indent=2))
    for category,entry in first_difference.items():print(category,'first mismatch tick',entry['tick'],entry['differences'][:3])
    return report['passed']

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True);p.add_argument('--evaluator',type=Path,required=True)
    p.add_argument('--output',type=Path,help='separate audit directory to preserve earlier comparisons')
    raise SystemExit(not audit(p.parse_args()))
