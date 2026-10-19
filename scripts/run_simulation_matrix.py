#!/usr/bin/env python3
"""Build one diagnostic binary and capture repeatable simulation workloads."""
import argparse,hashlib,json
from pathlib import Path
from build_saturn_variant import build
from run_simulation_profile import run
from saturn_probe import parser as probe_parser

SCENARIOS={
    'neutral':'',
    'approach':'320 1 buttons=RIGHT\n500 1 buttons=NONE\n700 1 buttons=LEFT\n900 1 buttons=NONE\n',
    'attacks':('20 1 buttons=RIGHT\n100 1 buttons=NONE\n110 2 buttons=LEFT\n'
               '320 1 buttons=X\n400 1 buttons=NONE\n450 1 buttons=DOWN+A\n'
               '530 1 buttons=NONE\n580 1 buttons=RIGHT+DOWN+X\n660 1 buttons=NONE\n'
               '710 1 buttons=X\n790 1 buttons=NONE\n840 1 buttons=DOWN+A\n920 1 buttons=NONE\n'),
    'simultaneous':('20 1 buttons=RIGHT\n20 2 buttons=LEFT\n100 1 buttons=NONE\n100 2 buttons=NONE\n'
                    '320 1 buttons=X\n320 2 buttons=X\n400 1 buttons=NONE\n400 2 buttons=NONE\n'
                    '500 1 buttons=X\n500 2 buttons=X\n580 1 buttons=NONE\n580 2 buttons=NONE\n'
                    '700 1 buttons=X\n700 2 buttons=X\n780 1 buttons=NONE\n780 2 buttons=NONE\n')}

def execute(a):
    root=a.out.resolve()
    if root.exists() and any(root.iterdir()):raise FileExistsError('use a fresh output directory')
    root.mkdir(parents=True,exist_ok=True)
    target=root/'build'
    build(a.baseline,target,['-DT3_SIM_PROFILE=ON','-DT3_PERF_SAMPLES=OFF',
          '-DT3_VERIFY_PARALLEL_GEOMETRY=OFF','-DT3_RENDER_BENCH_TICK=0','-DT3_PERF_MODE=0'])
    cases=[(name,text,a.frames) for name,text in SCENARIOS.items()]
    if a.ko_script:cases.append(('ko-script',a.ko_script.read_text(),a.ko_fields))
    report=dict(completed=False,cases={},scope='Instrumented workloads, not performance or combat qualification. '
                'Fixed-field inputs need not reach the same logic tick as an uninstrumented run. '
                'KO coverage must be established from observed states, never from the scenario label.')
    for name,text,fields in cases:
        script=root/(name+'.devices')
        script.write_text('0 1 buttons=NONE\n0 2 buttons=NONE\n'+text)
        args=probe_parser().parse_args(['--build',str(target),'--bios',str(a.bios.resolve()),
             '--out',str(root/name),'--frames',str(fields),'--timeout',str(a.timeout),
             '--sh2-cache','on','--device-script',str(script)])
        profile=run(args)
        report['cases'][name]=dict(profile=str(root/name/'profile.json'),
            input_sha256=hashlib.sha256(script.read_bytes()).hexdigest(),
            aggregate_ticks=profile['aggregate_ticks'],tail_groups=profile['tail_groups'])
        (root/'matrix.json').write_text(json.dumps(report,indent=2)+'\n')
    report['completed']=True
    (root/'matrix.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True);p.add_argument('--bios',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);p.add_argument('--frames',type=int,default=1200)
    p.add_argument('--ko-script',type=Path);p.add_argument('--ko-fields',type=int,default=3200)
    p.add_argument('--timeout',type=int,default=300)
    a=p.parse_args()
    if a.frames<=300 or a.ko_fields<=300 or a.timeout<=0:p.error('require fields > warmup 300 and positive timeout')
    execute(a)
