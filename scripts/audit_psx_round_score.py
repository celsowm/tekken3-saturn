#!/usr/bin/env python3
"""Compare native VS scoring to original RoundWins entry/return RAM snapshots.

All retail observations and reports remain private. Trace values are checked
against compact snapshots before invoking the native scorer; no guest writes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

from analyze_psx_phases import analyze
from extract_psx_pose_tables import mapped
from prepare_psx_phase_capture import round_descriptors
from psx_disc_import import US_EXE_SHA
from psx_ram_oracle import FIGHTER_BASE, FIGHTER_STRIDE


class Ranges:
    def __init__(self, raw):
        if len(raw)<12 or len(raw)>8*1024*1024 or raw[:8]!=b'T3RANGES':
            raise ValueError('invalid compact RAM header/size')
        count=struct.unpack_from('<I',raw,8)[0]
        if not 1<=count<=32:raise ValueError('invalid compact range count')
        self.ranges=[]
        cursor=12
        for _ in range(count):
            if cursor+8>len(raw):raise ValueError('truncated range header')
            address,size=struct.unpack_from('<II',raw,cursor);cursor+=8
            if not size or not 0x80000000<=address<address+size<=0x80200000 or cursor+size>len(raw):
                raise ValueError('invalid/truncated RAM range')
            data=raw[cursor:cursor+size];cursor+=size
            for old,previous in self.ranges:
                lo,hi=max(old,address),min(old+len(previous),address+size)
                if lo<hi and previous[lo-old:hi-old]!=data[lo-address:hi-address]:
                    raise ValueError('conflicting overlapping RAM ranges')
            self.ranges.append((address,data))
        if cursor!=len(raw):raise ValueError('trailing compact RAM data')

    def read(self,address,size,signed=False):
        for start,data in self.ranges:
            if start<=address and address+size<=start+len(data):
                return int.from_bytes(data[address-start:address-start+size],'little',signed=signed)
        raise ValueError(f'uncaptured RAM field {address:#x}')


def checked_state(row,raw,fields):
    ram=Ranges(raw)
    if ram.read(0x800AFA88,4)!=1:raise ValueError('only normal VS mode is supported')
    values={name:ram.read(f['address'],f['size'],f['signed']) for name,f in fields.items()}
    if row['round']!=values:raise ValueError('round JSON/RAM disagreement')
    pair=[]
    for i in range(2):
        base=FIGHTER_BASE+i*FIGHTER_STRIDE
        fighter={name:ram.read(base+offset,2,True) for name,offset in
                 [('round_wins',0x44),('round_won',0x46),('health_left',0x400)]}
        if any(row['fighters'][i][name]!=v for name,v in fighter.items()):
            raise ValueError('fighter score JSON/RAM disagreement')
        pair.append(fighter)
    return values,pair


def audit(capture,runner,layout_path):
    provenance=json.loads((capture/'provenance.json').read_text())
    layout_raw=layout_path.read_bytes()
    if provenance['exe_sha256']!=US_EXE_SHA or provenance['layout_sha256']!=hashlib.sha256(layout_raw).hexdigest():
        raise ValueError('unverified capture executable/layout')
    layout=json.loads(layout_raw)['blocks']['exe'];points=dict(layout['points'])
    fields=round_descriptors(lambda a:points.get(a) or mapped(layout,a,4))
    if fields!=provenance['round_fields']:raise ValueError('obsolete/unverified round field mapping')
    trace=(capture/'phases.jsonl').read_bytes()
    rows=[json.loads(line) for line in trace.splitlines()]
    analyze(rows)  # Reject incomplete or incorrectly nested call traces.
    pending={};results=[];snapshots={}
    for row in rows:
        if row['phase'] not in ('RoundWins:entry','RoundWins:exit'):continue
        edge='before' if row['phase'].endswith('entry') else 'after'
        raw=(capture/f"round-{row['call']}-{edge}.ranges").read_bytes()
        snapshots[f"round-{row['call']}-{edge}.ranges"]=hashlib.sha256(raw).hexdigest()
        state=checked_state(row,raw,fields)
        if edge=='before':pending[row['call']]=state;continue
        before,pair=pending.pop(row['call'])
        values=[p['health_left'] for p in pair]+[p['round_wins'] for p in pair]
        values += [before[k] for k in ('played','to_win','max_rounds','human_mask','tie_choice',
                                       'draw_wins','match_result','match_winner','match_loser')]
        actual=json.loads(subprocess.check_output([str(runner),*map(str,values)],text=True))
        after,pair=state
        expected=dict(wins=[p['round_wins'] for p in pair],round_won=[p['round_won'] for p in pair],
                      **{k:after[k] for k in ('max_rounds','match_result','match_winner','match_loser')})
        if actual!=expected:raise ValueError(f"native/original scoring disagreement at call {row['call']}: {actual} != {expected}")
        results.append(dict(call=row['call'],source_frame=row['source_frame'],inputs=values,output=actual))
    if pending or not results:raise ValueError('missing paired original score observations')
    return dict(format=1,scope='normal VS RoundWins/MatchResult only',
                trace_sha256=hashlib.sha256(trace).hexdigest(),
                runner_sha256=hashlib.sha256(runner.read_bytes()).hexdigest(),
                snapshot_sha256=snapshots,
                paired_rounds=len(results),all_equal=True,results=results)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True)
    p.add_argument('--runner',type=Path,required=True)
    p.add_argument('--layout',type=Path,default=Path(__file__).resolve().parents[1]/'third_party/Tekken3Remake/tools/remake_import/layouts/usa.json')
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();result=audit(a.capture,a.runner.resolve(),a.layout)
    a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
