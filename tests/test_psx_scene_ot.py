#!/usr/bin/env python3
"""Compare mixed fighter/particle traversal to captured original OT links."""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import random
import subprocess
ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--runner',type=Path,required=True);args=parser.parse_args()
    trace=ROOT/'build/parity-fighter-particle-sequenced.json'
    if not trace.is_file():return 77
    data=json.loads(trace.read_text());events=[]
    for kind,name in enumerate(('fighter_events','particle_links')):
        for e in data[name]:events.append(dict(e,kind=kind))
    if not events or any('serial' not in e for e in events):return 77
    grouped=defaultdict(list)
    for e in events:grouped[e['frame']].append(e)
    cases=[];expected=[];mixed=0;links=0
    for frame,rows in sorted(grouped.items()):
        rows.sort(key=lambda e:e['serial']);by_bucket=defaultdict(dict);heads={}
        for index,e in enumerate(rows):
            bucket=e['bucket'];pointer=int(e['value'],16);previous=int(e['previous'],16)
            if bucket in heads:
                if previous!=heads[bucket]:raise AssertionError(f'broken original OT link at frame {frame}')
                links+=1
            if pointer in by_bucket[bucket]:raise AssertionError('duplicate packet in a bucket')
            by_bucket[bucket][pointer]=(index,previous);heads[bucket]=pointer
        order=[]
        for bucket in sorted(heads,reverse=True):
            nodes=by_bucket[bucket];pointer=heads[bucket];kinds=set()
            while pointer in nodes:
                index,pointer=nodes.pop(pointer);order.append(index);kinds.add(rows[index]['kind'])
            if nodes:raise AssertionError('unreachable original OT link')
            mixed+=len(kinds)>1
        cases.append([(e['bucket'],1,e['kind']) for e in rows]);expected.append(order)
    rng=random.Random(0x5173)
    for _ in range(100):
        case=[(rng.randrange(1024),rng.randrange(2),rng.randrange(3)) for _ in range(rng.randrange(100))]
        cases.append(case);expected.append(sorted((i for i,e in enumerate(case) if e[1]),key=lambda i:(-case[i][0],-i)))
    payload=''.join(str(len(c))+' '+' '.join(str(v) for entry in c for v in entry)+'\n' for c in cases)
    run=subprocess.run([str(args.runner.resolve())],input=payload,text=True,capture_output=True,check=True)
    if [json.loads(line) for line in run.stdout.splitlines()]!=expected:raise AssertionError('mixed OT order differs')
    print(json.dumps(dict(frames=len(grouped),primitives=len(events),linked_pairs=links,mixed_buckets=mixed,random_cases=100)))
    return 0
if __name__=='__main__':raise SystemExit(main())
