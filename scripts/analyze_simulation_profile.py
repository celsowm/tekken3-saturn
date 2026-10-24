#!/usr/bin/env python3
"""Decode bounded nested simulation timings; never infer release FPS."""
import argparse,json,math,struct
from pathlib import Path

STAGES=('tick','match','copy','move_begin','ground','root','motion','fk','ik',
        'blend','head','parts','compose','bodies','contact','hits','branch',
        'branch_rows','command_calls','bridge','cache_hit','cache_miss',
        'step','ai','camera','local_pose','world_root','slots','shapes','sample')
ACTORS=('p1','p2','shared')

CAPACITY=128 # default retained rows; the header gives each capture's own
EVENTS={'branch_rows','command_calls','cache_hit','cache_miss'}

class IncompleteProfileError(ValueError):
    """Guest stopped inside publication; only this condition may retry."""

def decode(raw):
    counters=len(STAGES)*len(ACTORS);row_words=7+3*counters
    magic,version,hz,count,cursor,epoch,warmup,stages,actors,capacity,errors=struct.unpack_from('>11I',raw)
    if (magic,version,stages,actors)!=(0x54335350,1,len(STAGES),len(ACTORS)) or not capacity:
        raise ValueError('unsupported simulation profile schema')
    # T3_SIM_PROFILE_CAPACITY retained rows (CAPACITY unless a build lowers it).
    if len(raw)!=4*(11+3*counters+capacity*row_words):raise ValueError('invalid simulation profile size')
    if errors:raise ValueError('guest profiler error: '+hex(errors))
    if epoch&1:raise IncompleteProfileError('guest stopped inside profiler publication')
    if not hz or not count or cursor!=count%capacity or epoch!=(2*count)&0xffffffff:
        raise ValueError('unpublished simulation profile')
    def table(at):
        values=struct.unpack_from('>'+str(3*counters)+'I',raw,at)
        return [dict(zip(('inclusive','exclusive','calls'),values[i:i+3]))
                for i in range(0,len(values),3)]
    root_index=2*len(STAGES)
    def validate(values,expected_calls):
        root=values[root_index]
        if root['calls']!=expected_calls or not root['inclusive'] or any(
                c['exclusive']>c['inclusive'] or
                (not c['calls'] and (c['inclusive'] or c['exclusive'])) for c in values):
            raise ValueError('invalid profiler counters')
        if sum(c['exclusive'] for c in values)!=root['inclusive']:
            raise ValueError('exclusive timings do not reconcile with the root')
        for i,c in enumerate(values):
            if STAGES[i%len(STAGES)] in EVENTS and (c['inclusive'] or c['exclusive']):
                raise ValueError('event counters must not contain timings')
    totals=table(44);validate(totals,count)
    rows=[];base=44+12*counters
    indices=range(count) if count<capacity else [(cursor+i)%capacity for i in range(capacity)]
    for i in indices:
        at=base+4*row_words*i
        field,tick,state,replay,active,error,depth=struct.unpack_from('>7I',raw,at)
        if field<warmup or error or depth or active>3:raise ValueError('failed/unclosed simulation tick')
        values=table(at+28);validate(values,1)
        rows.append(dict(field=field,tick=tick,state=state,replay=replay,active=active,counters=values))
    if any(b['field']<a['field'] for a,b in zip(rows,rows[1:])):
        raise ValueError('non-monotonic profile fields')
    for i,total in enumerate(totals):
        if any(sum(r['counters'][i][key] for r in rows)>total[key] for key in total):
            raise ValueError('retained rows exceed aggregate counters')
    return dict(frequency=hz,count=count,warmup=warmup,totals=totals,rows=rows)

def summarize(data):
    hz=data['frequency'];totals=data['totals'];count=data['count']
    root=totals[2*len(STAGES)]['inclusive']
    def metric(c):
        return dict(**c,inclusive_ms_per_tick=c['inclusive']*1000/hz/count,
            exclusive_ms_per_tick=c['exclusive']*1000/hz/count,
            exclusive_share_percent=c['exclusive']*100/root,
            inclusive_us_per_call=c['inclusive']*1e6/hz/c['calls'] if c['calls'] else None)
    stages={}
    for i,name in enumerate(STAGES):
        values={key:sum(totals[a*len(STAGES)+i][key] for a in range(3))
                for key in ('inclusive','exclusive','calls')}
        stages[name]=dict(metric(values),kind='event' if name in EVENTS else 'timed',
            actors={actor:metric(totals[a*len(STAGES)+i]) for a,actor in enumerate(ACTORS)})
    def stats(values):
        values=sorted(values)
        return dict(median_ms=values[len(values)//2]*1000/hz,
                    p95_ms=values[math.ceil(.95*len(values))-1]*1000/hz,
                    max_ms=values[-1]*1000/hz)
    groups={}
    for row in data['rows']:
        key=f"state={row['state']},replay={row['replay']},active={row['active']}"
        groups.setdefault(key,[]).append(row)
    tail={}
    for key,rows in groups.items():
        phase_stages={}
        for i,name in enumerate(STAGES):
            values={field:sum(r['counters'][a*len(STAGES)+i][field]
                    for r in rows for a in range(3))
                    for field in ('inclusive','exclusive','calls')}
            phase_stages[name]=dict(calls=values['calls'],
                inclusive_ms_per_tick=values['inclusive']*1000/hz/len(rows),
                exclusive_ms_per_tick=values['exclusive']*1000/hz/len(rows))
        tail[key]=dict(ticks=len(rows),first_field=rows[0]['field'],last_field=rows[-1]['field'],
            root=stats([r['counters'][2*len(STAGES)]['inclusive'] for r in rows]),stages=phase_stages)
    return dict(frequency=hz,after_field=data['warmup'],aggregate_ticks=count,
        retained_ticks=len(data['rows']),first_retained_field=data['rows'][0]['field'],
        last_retained_field=data['rows'][-1]['field'],stages=stages,tail_groups=tail,
        scope='Instrumented Master-only nested FRT intervals including hooks and interrupts. '
              'Aggregates start at after_field; percentiles cover only the retained tail. '
              'Actor ownership is attributed only inside place/compose; other work is shared. '
              'Inclusive stages overlap; exclusive sums reconcile to the tick root. '
              'Event counts carry no timing. Not release cadence or hardware certification.')

def analyze(capture):
    manifest=json.loads((capture/'symbol-dumps.json').read_text())
    symbol=next(v for k,v in manifest.items() if k.lstrip('_')=='g_t3_sim_profile')
    report=summarize(decode((capture/symbol['file']).read_bytes()))
    report['provenance']=json.loads((capture/'observation.json').read_text())['provenance']
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,help='saturn_probe output with symbol dumps')
    p.add_argument('--raw',type=Path,help='raw g_t3_sim_profile bytes, e.g. mednafen_state.py --dump')
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    if bool(a.capture)==bool(a.raw):p.error('give exactly one of --capture and --raw')
    report=analyze(a.capture) if a.capture else summarize(decode(a.raw.read_bytes()))
    a.out.write_text(json.dumps(report,indent=2)+'\n')
