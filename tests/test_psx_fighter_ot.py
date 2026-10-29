#!/usr/bin/env python3
"""Check fighter OT arithmetic against observed stores in original code."""
import argparse
import json
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--runner',type=Path,required=True);a=p.parse_args()
    trace=ROOT/'build/parity-fighter-particle-order-original.json'
    if not trace.is_file():return 77
    j=json.loads(trace.read_text());events=j['fighter_events']
    if not events or 'descriptor' not in events[0]:return 77
    rows=[(e['first_depth_bucket'],e['last_depth_bucket'],e['descriptor'],e['nclip']) for e in events]
    r=subprocess.run([str(a.runner.resolve())],input=''.join(' '.join(map(str,x))+'\n' for x in rows),
                     text=True,capture_output=True,check=True)
    got=[int(line) for line in r.stdout.splitlines()];expected=[e['bucket'] for e in events]
    if got!=expected:raise AssertionError('C fighter OT arithmetic differs from actual original stores')
    print(json.dumps(dict(matched_fighter_ot_stores=len(got),quad=sum(e['quad'] for e in events),
                          triangle=sum(not e['quad'] for e in events),
                          scope='Observed JP Rev.1 fighter OT stores; vertex selection and native rendering remain separate')))
    return 0
if __name__=='__main__':raise SystemExit(main())
