#!/usr/bin/env python3
"""Compile an explicitly private, simulation-tick input fixture (never game state)."""
import argparse,json
from pathlib import Path

def build(source,dest):
    data=json.loads(source.read_text());rows=data['events']
    if not rows or rows[0][0]!=0:raise ValueError('input tape must start at tick zero')
    previous=-1
    for row in rows:
        if len(row)!=5 or any(type(v)!=int or v<0 or v>65535 for v in row):raise ValueError('invalid input row')
        if row[0]<=previous:raise ValueError('input ticks must strictly increase')
        if any(v&~0x7ff for v in row[1:]):raise ValueError('unknown input bit')
        previous=row[0]
    dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text('/* Private captured inputs; no game state or retail bytes. */\n'
                   '#define T3_PRIVATE_INPUT_EVENTS '+str(len(rows))+'u\n'
                   'static const uint16_t t3_private_input_tape[][5]={\n'+
                   ''.join('{'+','.join(str(v)+'u' for v in row)+'},\n' for row in rows)+'};\n')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('dest',type=Path)
    a=p.parse_args();build(a.source,a.dest)
