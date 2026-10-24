#!/usr/bin/env python3
"""Build a PRIVATE one-seed/external-input oracle fixture, never expected states."""
import argparse,hashlib,json,struct
from pathlib import Path

SCHEMA_FILES=('tools/psx_match_reference.inc','tools/psx_ground_eval_fields.h','tools/psx_round_flow_eval_fields.h')
VIEW_HEADER=12
VIEW_BYTES=38

def load(root):
    manifest=json.loads((root/'manifest.json').read_text())
    project=Path(__file__).resolve().parents[1]
    schema=hashlib.sha256(b''.join((project/n).read_bytes() for n in SCHEMA_FILES)).hexdigest()
    if manifest.get('schema')!=1 or manifest.get('codec')!='sha256-i64be-v1' or \
            manifest.get('audit_passed') is not True or manifest.get('observation_schema_sha256')!=schema:
        raise ValueError('requires a passing original audit with the current observation schema')
    for name in ('seed.ranges','seed-addresses.json','inputs.tsv','expected.sha256'):
        if hashlib.sha256((root/name).read_bytes()).hexdigest()!=manifest.get('files',{}).get(name):
            raise ValueError('changed private reference export: '+name)
    rows=[list(map(int,line.split())) for line in (root/'inputs.tsv').read_text().splitlines()]
    if not 1<=len(rows)<=6000 or len(rows)!=manifest.get('ticks') or \
            any(len(r)!=len(rows[0]) for r in rows) or len(rows[0]) not in (10,12):
        raise ValueError('invalid bounded reference input trace')
    if len((root/'expected.sha256').read_bytes())!=32*len(rows):
        raise ValueError('expected digest count differs from input count')
    if any(not 0<=r[0]<=0xffffffff or any(not 0<=v<=65535 for v in r[1:9]) or
           not 1<=r[9]<=65535 or (len(r)==12 and any(v not in (0,1) for v in r[10:12]))
           for r in rows):
        raise ValueError('invalid input/frame/field-count word')
    views=None
    if 'views.bin' in manifest.get('files',{}):
        data=(root/'views.bin').read_bytes()
        if hashlib.sha256(data).hexdigest()!=manifest['files']['views.bin']:
            raise ValueError('changed private reference export: views.bin')
        if len(data)<16 or data[:8]!=b'T3VIEWS\0':
            raise ValueError('invalid bounded reference draw trace')
        count=struct.unpack_from('<I',data,8)[0]
        screens_at=12+VIEW_BYTES*count
        if not 1<=count<=256 or len(data)<screens_at+4 or \
                struct.unpack_from('<I',data,screens_at)[0]!=len(rows) or \
                len(data)!=screens_at+4+4*len(rows):
            raise ValueError('invalid bounded reference draw trace')
        ticks=[struct.unpack_from('<I',data,12+VIEW_BYTES*i)[0] for i in range(count)]
        if ticks!=sorted(set(ticks)) or ticks[-1]>=len(rows):
            raise ValueError('invalid reference view ticks')
        views=(data[12:screens_at],data[screens_at+4:])
    seed=(root/'seed.ranges').read_bytes()
    if len(seed)<12 or seed[:8]!=b'T3RANGES':raise ValueError('invalid compact seed')
    count=struct.unpack_from('<I',seed,8)[0];at=12
    if not 1<=count<=64:raise ValueError('invalid seed range count')
    spans=[]
    for _ in range(count):
        if at+8>len(seed):raise ValueError('truncated seed entry')
        address,size=struct.unpack_from('<II',seed,at);at+=8
        if not size or address<0x80000000 or address+size>0x80200000 or at+size>len(seed):
            raise ValueError('invalid seed span')
        if any(address<end and start<address+size for start,end in spans):raise ValueError('overlapping seed spans')
        spans.append((address,address+size));at+=size
    if at!=len(seed):raise ValueError('trailing seed bytes')
    return manifest,rows,seed,views

def emit_bytes(name,data):
    data=data or b'\0'
    return 'static const uint8_t '+name+'[] = {\n'+''.join(
        '    '+','.join(map(str,data[i:i+24]))+',\n' for i in range(0,len(data),24))+'};\n'

def generate(root,out):
    manifest,rows,seed,views=load(root)
    addresses=json.loads((root/'seed-addresses.json').read_text())
    if set(addresses)!={'step_x','step_z','previous','ramp'} or any(
            not isinstance(v,int) or not 0x80000000<=v<0x80200000 for v in addresses.values()):
        raise ValueError('invalid seed global addresses')
    events=[];last=None;deltas=bytearray((len(rows)+2)//4)
    for tick,row in enumerate(rows):
        values=tuple(row[1:9])+(tuple(row[10:12]) if len(row)==12 else (0,0))
        if values!=last:events.append((tick,values));last=values
        if tick:
            delta=row[0]-rows[tick-1][0]
            if delta not in (0,1,2,4):raise ValueError('unrepresentable original source-frame delta')
            code={0:0,1:1,2:2,4:3}[delta]
            deltas[(tick-1)//4]|=code<<(2*((tick-1)%4))
    text='''/* PRIVATE reference replay: initial entry RAM and external IO only.
 * Expected per-tick digests remain on the host, never in the Saturn image. */
#ifndef T3_REFERENCE_FIXTURE_H
#define T3_REFERENCE_FIXTURE_H
#include <stdint.h>
#include "psx_match.h"
typedef struct {uint16_t tick,words[10];} t3_reference_event_t;
'''
    text+=f'#define T3_REFERENCE_TICKS {len(rows)}u\n#define T3_REFERENCE_FIRST_FRAME {rows[0][0]}u\n'
    text+=f'#define T3_REFERENCE_HAS_SIDES {int(len(rows[0])==12)}\n'
    view_count=len(views[0])//VIEW_BYTES if views is not None else 0
    text+=f'#define T3_REFERENCE_HAS_VIEW {int(view_count!=0)}\n'
    text+=f'#define T3_REFERENCE_VIEWS {view_count}u\n'
    identity=hashlib.sha256((root/'manifest.json').read_bytes()).digest()
    text+='#define T3_REFERENCE_FIXTURE_ID {'+','.join(map(str,identity))+'}\n'
    for name,value in addresses.items():text+=f'#define T3_REFERENCE_{name.upper()} {value}u\n'
    text+=emit_bytes('t3_reference_seed',seed)+emit_bytes('t3_reference_deltas',deltas)
    if views is not None:
        text+=emit_bytes('t3_reference_views',views[0])+emit_bytes('t3_reference_screens',views[1])
    text+=f'#define T3_REFERENCE_EVENTS {len(events)}u\nstatic const t3_reference_event_t t3_reference_events[] = {{\n'
    text+=''.join('    {'+str(t)+',{'+','.join(map(str,v))+'}},\n' for t,v in events)+'};\n#endif\n'
    out.parent.mkdir(parents=True,exist_ok=True);out.write_text(text)
    return dict(ticks=len(rows),events=len(events),seed_bytes=len(seed),delta_bytes=len(deltas),
        view_bytes=len(views[0]) if views is not None else 0,
        screen_bytes=len(views[1]) if views is not None else 0,
        input_bytes=len(events)*22,expected_state_bytes_in_image=0,trace_sha256=manifest['trace_sha256'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('export',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args()
    try:print(json.dumps(generate(a.export,a.output),indent=2))
    except (OSError,ValueError,KeyError) as e:p.exit(1,f'reference fixture failed: {e}\n')
