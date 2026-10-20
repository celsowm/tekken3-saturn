#!/usr/bin/env python3
"""Attribute harness interpreter costs to a matching ELF. No retail data needed."""
import argparse,bisect,collections,csv,hashlib,json,math,statistics,struct,subprocess,tempfile
from pathlib import Path

def read_symbols(elf,nm):
    text=subprocess.check_output([str(nm),'-n','-S','-C','--defined-only',str(elf)],text=True)
    result=[]
    for line in text.splitlines():
        parts=line.split(None,3)
        if len(parts)==4 and parts[2].lower() in ('t','w'):
            result.append((int(parts[0],16),int(parts[1],16),parts[3]))
        elif len(parts)==3 and parts[1].lower() in ('t','w'):
            result.append((int(parts[0],16),0,parts[2]))
    return sorted(result)

def attribute(rows,symbols,totals):
    # Sized ELF functions take precedence over zero-sized assembler aliases.
    sized=[s for s in symbols if s[1]];starts=[s[0] for s in sized]
    all_starts=sorted(set(s[0] for s in symbols))
    zero={s[0]:s[2] for s in symbols if not s[1]}
    costs=collections.defaultdict(lambda:collections.Counter())
    for row in rows:
        pc=int(row['pc'])&0x1fffffff
        cpu=row['cpu'];n=int(row['instructions']);cycles=int(row['execution_cycles'])
        if cpu not in ('master','slave') or min(n,cycles)<0:raise ValueError('invalid CPU cost row')
        i=bisect.bisect_right(starts,pc)-1
        if i>=0 and pc<sum(sized[i][:2]):
            name=sized[i][2];boundary='elf_size'
        else:
            i=bisect.bisect_right(all_starts,pc)-1
            if i>=0 and i+1<len(all_starts) and all_starts[i] in zero:
                name=zero[all_starts[i]];boundary='inferred_next_text_symbol'
            else:name='unresolved';boundary='unresolved'
        key=(cpu,name,boundary);costs[key]['instructions']+=n;costs[key]['execution_cycles']+=cycles
    result={}
    for cpu in ('master','slave'):
        items=[dict(symbol=name,boundary=boundary,**cost) for (target,name,boundary),cost in costs.items() if target==cpu]
        instruction_sum=sum(x['instructions'] for x in items)
        cycle_sum=sum(x['execution_cycles'] for x in items)
        total=totals[cpu]
        if instruction_sum!=total['instructions'] or cycle_sum+total['interrupt_entry_cycles']!=total['execution_cycles']:
            raise ValueError(cpu+' histogram does not reconcile with profile totals')
        for item in items:
            item['percent_execution_cycles']=100*item['execution_cycles']/total['execution_cycles'] if total['execution_cycles'] else 0
        result[cpu]=dict(totals=total,functions=sorted(items,key=lambda x:-x['execution_cycles']))
    return result

def completed_calls(rows,symbols,selected=(),totals=None):
    """Pair observed SH-2 calls with their exact PC+4 return address.

    Costs are inclusive pre-call to pre-RTS interpreter costs on ONE CPU.
    They include child work and interrupts, omit the returning RTS/delay slot,
    and are neither scheduler timestamps nor a reconstructed global timeline.
    Unmatched returns (including GCC movstr entry jumps) do not pop callers.
    Calls skipped by a nonlocal return are excluded and counted explicitly.
    """
    names={address:name for address,size,name in symbols}
    stacks={cpu:[] for cpu in ('master','slave')}
    groups={cpu:collections.defaultdict(list) for cpu in stacks}
    intervals={cpu:[] for cpu in stacks}
    checks={cpu:collections.Counter() for cpu in stacks}
    last={cpu:-1 for cpu in stacks}
    last_field={cpu:-1 for cpu in stacks}
    events={cpu:collections.Counter() for cpu in stacks}
    selected=set(selected)
    for row in rows:
        cpu=row['cpu']
        if cpu not in stacks:raise ValueError('invalid call-span CPU')
        clock=int(row['execution_cycles_before_step'])
        if clock<last[cpu] or clock<0:raise ValueError('non-monotonic CPU event costs')
        frame=int(row['frame'])
        if frame<last_field[cpu] or frame<0:raise ValueError('non-monotonic CPU event fields')
        if row['event'] not in ('call','return','interrupt','div32','div64','dma0','dma1'):
            raise ValueError('unsupported CPU event')
        if totals is not None and clock>totals[cpu]['execution_cycles']:
            raise ValueError('CPU event cost exceeds profile total')
        last[cpu]=clock
        last_field[cpu]=frame
        events[cpu][row['event']]+=1
        stack=stacks[cpu]
        if row['event']=='call':
            target=int(row['target'])&0x1fffffff
            stack.append(dict(return_pc=(int(row['pc'])+4)&0x1fffffff,
                              start=clock,frame=frame,
                              symbol=names.get(target,hex(target))))
        elif row['event']=='return':
            target=int(row['target'])&0x1fffffff
            match=next((i for i in range(len(stack)-1,-1,-1)
                        if stack[i]['return_pc']==target),None)
            if match is None:
                checks[cpu]['unmatched_returns']+=1
                continue
            checks[cpu]['calls_skipped_by_return']+=len(stack)-match-1
            entry=stack[match]
            del stack[match:]
            cost=clock-entry['start']
            groups[cpu][entry['symbol']].append(cost)
            if entry['symbol'] in selected:
                intervals[cpu].append(dict(symbol=entry['symbol'],
                    first_field=entry['frame'],last_field=int(row['frame']),
                    start_cpu_cost=entry['start'],end_cpu_cost=clock,cpu_cost=cost))
    result={}
    for cpu in stacks:
        if totals is not None:
            observed=dict(calls=events[cpu]['call'],returns=events[cpu]['return'],
                interrupts=events[cpu]['interrupt'],
                divisions=events[cpu]['div32']+events[cpu]['div64'],
                dma_transfers=events[cpu]['dma0']+events[cpu]['dma1'])
            if any(count!=totals[cpu][key] for key,count in observed.items()):
                raise ValueError(cpu+' event counts do not reconcile with profile totals')
        summaries=[]
        for name,costs in groups[cpu].items():
            costs.sort()
            summaries.append(dict(symbol=name,calls=len(costs),inclusive_cpu_cost=sum(costs),
                minimum_cpu_cost=costs[0],median_cpu_cost=statistics.median(costs),
                p95_cpu_cost=costs[math.ceil(len(costs)*.95)-1],maximum_cpu_cost=costs[-1]))
        result[cpu]=dict(functions=sorted(summaries,key=lambda item:-item['inclusive_cpu_cost']),
            selected_intervals=intervals[cpu],open_calls_at_end=len(stacks[cpu]),
            event_counts=dict(events[cpu]),
            unmatched_returns=checks[cpu]['unmatched_returns'],
            calls_skipped_by_return=checks[cpu]['calls_skipped_by_return'])
    return result


def analyze(prefix,elf,nm,span_symbols=()):
    prefix=Path(prefix);elf=Path(elf)
    meta=json.loads(Path(str(prefix)+'.json').read_text())
    if meta.get('version')!=1 or meta.get('truncated',True):raise ValueError('unsupported or truncated CPU profile')
    if meta.get('frames_stored',0)!=meta.get('trace_frames_requested'):
        raise ValueError('incomplete CPU profile fields')
    if meta.get('dropped_frames') or any(meta[cpu].get(key,0)
            for cpu in ('master','slave')
            for key in ('dropped_pc_observations','dropped_events','dropped_snapshots')):
        raise ValueError('incomplete CPU profile')
    symbols=read_symbols(elf,nm)
    with Path(str(prefix)+'.pc.csv').open() as f:
        cpus=attribute(list(csv.DictReader(f)),symbols,meta)
    with Path(str(prefix)+'.events.csv').open() as f:
        calls=completed_calls(csv.DictReader(f),symbols,span_symbols,meta)
    return dict(version=1,elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                scope='Exclusive interpreter-step cost by PC, not inclusive call time; inferred assembler boundaries require disassembly review.',
                profile=meta,cpus=cpus,completed_calls=calls,
                call_span_scope='Inclusive pre-call to pre-RTS interpreter cost per CPU; '
                    'includes children/interrupts, excludes final RTS/delay slot. '
                    'Only completed observed calls; boundary calls and unmatched returns are reported. '
                    'Nested spans overlap. CPU cost clocks must not be aligned across CPUs '
                    'or treated as elapsed hardware time.')

def disassemble_snapshots(prefix,objdump,limit=64):
    """Decode captured words, not a later overlay or an ELF guessed to match it."""
    prefix=Path(prefix);seen=set();count=0;omitted=0
    with Path(str(prefix)+'.snapshots.csv').open() as source, \
         Path(str(prefix)+'.disassembly.txt').open('w') as out, \
         tempfile.TemporaryDirectory() as scratch:
        out.write('Captured pre-instruction words; at most 64 distinct CPU/PC/code combinations.\n')
        binary=Path(scratch)/'instructions.bin'
        for row in csv.DictReader(source):
            pc=int(row['pc']);words=tuple(int(row['opcode'+str(i)]) for i in range(8))
            key=(row['cpu'],pc,words)
            if key in seen:continue
            if count>=limit:omitted+=1;continue
            seen.add(key);count+=1
            binary.write_bytes(struct.pack('>8H',*words))
            out.write(f"\n{row['cpu']} frame={row['frame']} pc={pc:#x} pr={int(row['pr']):#x}\n")
            out.write(subprocess.check_output([str(objdump),'-D','-b','binary','-m','sh2','-EB',
                '--adjust-vma='+hex(pc),str(binary)],text=True))
        out.write(f'\nDecoded {count} combinations; omitted observations after limit: {omitted}\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prefix',required=True,type=Path);p.add_argument('--elf',required=True,type=Path)
    p.add_argument('--nm',default='sh2eb-elf-nm');p.add_argument('--out',required=True,type=Path)
    p.add_argument('--objdump',help='also disassemble captured checkpoint instruction words')
    p.add_argument('--span-symbol',action='append',default=[],
                   help='also retain completed invocation intervals for this exact ELF symbol')
    a=p.parse_args();a.out.write_text(json.dumps(analyze(a.prefix,a.elf,a.nm,a.span_symbol),indent=2)+'\n')
    if a.objdump:disassemble_snapshots(a.prefix,a.objdump)
