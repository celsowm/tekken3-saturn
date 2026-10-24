#!/usr/bin/env python3
"""Compare every instrumented Saturn tick with a passing original/host trace."""
import hashlib,json,struct
from pathlib import Path
import saturn_probe
from build_reference_replay import load

def compare(root,export,observation):
    manifest,rows,_,_=load(export)
    identity=hashlib.sha256((export/'manifest.json').read_bytes()).digest()
    if (root/'reference-fixture.bin').read_bytes()!=identity:
        raise ValueError('guest fixture identity differs from the selected export')
    def word(name):
        raw=(root/name).read_bytes()
        if len(raw)!=4:raise ValueError('invalid reference counter size')
        return struct.unpack('>I',raw)[0]
    count=word('reference-count.bin');error=word('reference-error.bin')
    trace=(root/'reference-trace.bin').read_bytes();fields=(root/'reference-fields.bin').read_bytes()
    expected=(export/'expected.sha256').read_bytes()
    if len(trace)!=len(expected) or len(fields)!=2*len(rows) or count>len(rows):
        raise ValueError('reference artifact size/counter mismatch')
    first=None
    for tick in range(count):
        actual_count=struct.unpack_from('>H',fields,2*tick)[0]
        if trace[32*tick:32*tick+32]!=expected[32*tick:32*tick+32] or actual_count!=rows[tick][9]:
            first=dict(tick=tick+1,source_frame=rows[tick][0],actual_fields=actual_count,
                expected_fields=rows[tick][9],actual_sha256=trace[32*tick:32*tick+32].hex(),
                expected_sha256=expected[32*tick:32*tick+32].hex());break
    complete=count==len(rows)
    result=dict(passed=complete and not error and first is None,complete=complete,
        ticks=count,expected_ticks=len(rows),matching_prefix=first['tick']-1 if first else count,
        error=error,first_difference=first,elf_sha256=observation['elf_sha256'],
        fixture_manifest_sha256=identity.hex(),
        provenance=observation['provenance'],trace_sha256=manifest['trace_sha256'],
        observation_schema_sha256=manifest['observation_schema_sha256'],
        expected_digests_sha256=hashlib.sha256(expected).hexdigest(),
        guest_trace_sha256=hashlib.sha256(trace[:count*32]).hexdigest(),
        scope='One original seed, then external mapped/physical pad IO, source-frame counters, '
              'per-tick screen X and sparse previous camera views used by RoundFlow; '
              'every declared field hashed as signed i64 big endian after each native tick. '
              'Rendered diagnostic build, not performance or independent-camera certification.')
    (root/'reference-comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

def execute(args):
    args.symbol_dumps=[('_g_reference_count','reference-count.bin'),
        ('_g_reference_error','reference-error.bin'),('_g_reference_trace','reference-trace.bin'),
        ('_g_reference_fixture_id','reference-fixture.bin'),
        ('_g_reference_value_counts','reference-fields.bin')]
    observation=saturn_probe.run(args)
    result=compare(args.out.resolve(),args.export.resolve(),observation)
    print(json.dumps(result,indent=2));return result['passed']

if __name__=='__main__':
    p=saturn_probe.parser();p.add_argument('--export',type=Path,required=True)
    p.set_defaults(build=saturn_probe.ROOT/'build/paul-reference',
                   out=saturn_probe.ROOT/'assets/imported/saturn-reference',frames=48000,timeout=600)
    raise SystemExit(not execute(p.parse_args()))
