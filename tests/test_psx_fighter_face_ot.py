#!/usr/bin/env python3
"""Check packed fighter faces against actual restored original draw stores."""
import argparse
import json
from pathlib import Path
import subprocess
from collections import defaultdict
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);a=p.parse_args()
    trace=ROOT/'build/parity-fighter-particle-order-original.json'
    if not trace.is_file():return 77
    events=json.loads(trace.read_text())['fighter_events']
    if not events or 'slot_depths' not in events[0]:return 77
    rows=[[e['group'],e['packed'],e['word1'],e['nclip'],*e['slot_depths']] for e in events]
    run=subprocess.run([str(a.runner.resolve())],input=''.join(' '.join(map(str,r))+'\n' for r in rows),
                       text=True,capture_output=True,check=True)
    got=[json.loads(line) for line in run.stdout.splitlines()]
    want=[[ *e['slots'],e['bucket'],e['descriptor']] for e in events]
    if got!=want:raise AssertionError('fighter face decoding or OT bucket differs from original')
    projected=[[e['group'],e['descriptor']>>24,*e['slot_depths'],
                *[v for xy in (e['packet_xy'] if e['quad'] else e['packet_xy']+[e['packet_xy'][-1]]) for v in xy]] for e in events]
    for mode in ('--projected','--native','--indexed'):
        run=subprocess.run([str(a.runner.resolve()),mode],input=''.join(' '.join(map(str,r))+'\n' for r in projected),
                           text=True,capture_output=True,check=True)
        if [json.loads(line) for line in run.stdout.splitlines()] != [[e['bucket'],e['nclip']] for e in events]:
            raise AssertionError(f'{mode}: projected coordinates or depth produce incorrect original face bucket')
    # Each OT store replaces the bucket head and records its previous link.
    # Verify the reversal used by the native queue against those actual links.
    buckets=defaultdict(list)
    for e in events:
        chain=buckets[e['frame'],e['bucket']]
        if chain and int(e['previous'],16)!=int(chain[-1]['value'],16):
            raise AssertionError('captured fighter bucket chain differs from insertion order')
        chain.append(e)
    links=sum(len(chain)-1 for chain in buckets.values())
    if links<3000:raise AssertionError('retail trace has too few linked fighter faces')
    print(json.dumps(dict(matched_original_faces=len(got),groups={str(i):sum(e['group']==i for e in events) for i in range(4)},
        scope='Original packed selection, flags and linked buckets; no native face-depth integration')))
    return 0
if __name__=='__main__':raise SystemExit(main())
