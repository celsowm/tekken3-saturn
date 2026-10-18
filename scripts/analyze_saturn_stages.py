#!/usr/bin/env python3
"""Decode the bounded, big-endian g_t3_perf diagnostic timing ring."""
import argparse,json,math,struct
from pathlib import Path

FIELDS=('field','ticks','elapsed','input','simulation','skeleton','skin',
        'placement','geometry','flush','hud','present','work')
CAPACITY=1024

def decode(raw):
    expected=12+CAPACITY*len(FIELDS)*4
    if len(raw)!=expected:raise ValueError(f'expected {expected} bytes, got {len(raw)}')
    version,count,cursor=struct.unpack_from('>III',raw)
    if version!=1 or not count or cursor!=count%CAPACITY:
        raise ValueError('invalid or unpublished stage ring')
    # The next slot may be getting overwritten when the emulator stops. In a
    # full ring skip it, even if count/cursor still describe the previous
    # publication. This also protects against a partially copied old sample.
    indices=range(count) if count<CAPACITY else [(cursor+i)%CAPACITY for i in range(1,CAPACITY)]
    rows=[dict(zip(FIELDS,struct.unpack_from('>'+len(FIELDS)*'I',raw,12+i*len(FIELDS)*4))) for i in indices]
    if any(b['field']<a['field'] for a,b in zip(rows,rows[1:])):
        raise ValueError('non-monotonic stage fields')
    return rows

def summarize(rows,after_field=180):
    rows=[r for r in rows if r['field']>=after_field]
    if not rows:raise ValueError('no samples after warmup')
    def stats(values):
        values=sorted(values)
        return dict(median=values[len(values)//2],p95=values[math.ceil(len(values)*.95)-1],maximum=values[-1])
    ticks=sum(r['ticks'] for r in rows)
    return dict(samples=len(rows),first_field=rows[0]['field'],last_field=rows[-1]['field'],
                simulation_ticks=ticks,stage_ms={k:stats([r[k] for r in rows]) for k in FIELDS[3:]},
                elapsed_fields=sum(r['elapsed'] for r in rows),
                elapsed_fields_over_four=sum(max(0,r['elapsed']-4) for r in rows),
                simulation_ms_per_tick=sum(r['simulation'] for r in rows)/ticks if ticks else None,
                scope='Instrumented stage samples; ms resolution; excludes outer VBlank/begin-frame wait. Geometry includes worker join; skeleton/skin are presentation work. Use uninstrumented endpoints for cadence.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('dump',type=Path);p.add_argument('--after-field',type=int,default=180)
    a=p.parse_args();print(json.dumps(summarize(decode(a.dump.read_bytes()),a.after_field),indent=2))
