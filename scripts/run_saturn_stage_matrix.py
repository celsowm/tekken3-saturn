#!/usr/bin/env python3
"""Build comparable private diagnostic variants from an existing optimized build."""
import argparse,hashlib,json,os,re,shlex,subprocess,sys
from pathlib import Path
from analyze_saturn_stages import decode,summarize

ROOT=Path(__file__).resolve().parents[1]
CASES={'full':(0,0),'simulation':(1,0),'render-one':(2,120),'render-two':(0,120)}

def project_overrides(options):
    if any(not re.match(r'^-DT3_[A-Z0-9_]+(?::(?:BOOL|STRING|PATH|FILEPATH))?=',option)
           for option in options):
        raise ValueError('variant overrides must be -DT3_NAME=value')
    return options

def cache_options(path):
    entries={}
    for line in path.read_text().splitlines():
        if line.startswith(('#','//')) or ':' not in line or '=' not in line:continue
        key,value=line.split('=',1);name,kind=key.split(':',1)
        if kind not in ('BOOL','STRING','FILEPATH','PATH'):continue
        entries[name]=(kind,value)
    if 'CMAKE_C_COMPILER' not in entries:
        metadata=list((path.parent/'CMakeFiles').glob('*/CMakeCCompiler.cmake'))
        if metadata:
            latest=max(metadata,key=lambda p:p.stat().st_mtime)
            match=re.search(r'^set\(CMAKE_C_COMPILER "([^"]+)"\)',latest.read_text(),re.M)
            if match:entries['CMAKE_C_COMPILER']=('FILEPATH',match.group(1))
    if entries.get('CMAKE_BUILD_TYPE',('',None))[1]!='Release':
        raise ValueError('baseline must be an optimized Release build')
    flags=entries.get('CMAKE_C_FLAGS_RELEASE',('',''))[1]
    if not {'-O2','-O3','-Os'}.intersection(shlex.split(flags)):
        raise ValueError('baseline Release C flags must contain -O2, -O3 or -Os')
    required=('CMAKE_TOOLCHAIN_FILE','CMAKE_C_COMPILER','LibSaturn_DIR','LIBSATURN_SOURCE_DIR','Python3_EXECUTABLE')
    for key in required:
        if not entries.get(key,('',''))[1]:raise ValueError('missing baseline cache entry: '+key)
    names=set(required)|{'CMAKE_BUILD_TYPE','CMAKE_PREFIX_PATH','CMAKE_C_FLAGS','CMAKE_CXX_FLAGS',
                         'CMAKE_C_FLAGS_RELEASE','CMAKE_CXX_FLAGS_RELEASE','CMAKE_EXE_LINKER_FLAGS',
                         'CMAKE_EXE_LINKER_FLAGS_RELEASE','LIBSATURN_TOOLCHAIN_ROOT','LIBSATURN_TOOLCHAIN_PREFIX'}
    options=['-D'+k+':'+t+'='+v for k,(t,v) in entries.items() if k in names or k.startswith('T3_')]
    return options,Path(entries['CMAKE_C_COMPILER'][1]).parent

def run(a):
    options,compiler_dir=cache_options(a.baseline.resolve()/'CMakeCache.txt')
    options+=project_overrides(a.cmake_option)
    out=a.out.resolve()
    if out.exists() and any(out.iterdir()):raise FileExistsError('use a fresh empty output directory')
    out.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ);env['PATH']=str(compiler_dir)+os.pathsep+env.get('PATH','')
    report=dict(scope='Instrumented neutral diagnostic matrix; drawing-removal modes and frozen poses do not qualify gameplay performance.',cases={})
    input_script=None
    if a.device_script:
        input_script=out/'input.devices'
        data=a.device_script.read_bytes();input_script.write_bytes(data)
        report['scope']='Instrumented scripted diagnostic matrix; drawing-removal modes and frozen poses do not qualify gameplay performance.'
        report['input_sha256']=hashlib.sha256(data).hexdigest()
    for name in a.cases:
        mode,tick=CASES[name];case=out/name;build=case/'build';capture=case/'capture'
        case.mkdir()
        commands=[['cmake','-S',str(ROOT),'-B',str(build),'-G','Ninja',*options,
                   '-DT3_PERF_SAMPLES=ON','-DT3_VERIFY_PARALLEL_GEOMETRY=OFF',
                   '-DT3_PERF_MODE='+str(mode),'-DT3_RENDER_BENCH_TICK='+str(tick)],
                  ['cmake','--build',str(build),'--parallel','4'],
                  [sys.executable,str(ROOT/'scripts/saturn_probe.py'),'--build',str(build),
                   '--bios',str(a.bios.resolve()),'--frames',str(a.frames),'--sh2-cache','on',
                   '--timeout',str(a.timeout),'--dump-symbol','g_t3_perf','--out',str(capture)]]
        if input_script:commands[-1]+=['--device-script',str(input_script)]
        (case/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        for i,command in enumerate(commands):
            with (case/f'step-{i}.log').open('w') as log:
                subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=a.timeout)
        manifest=json.loads((capture/'symbol-dumps.json').read_text())
        symbol=next(v for k,v in manifest.items() if k.lstrip('_')=='g_t3_perf')
        stages=summarize(decode((capture/symbol['file']).read_bytes()),a.after_field)
        (capture/'stages.json').write_text(json.dumps(stages,indent=2)+'\n')
        report['cases'][name]=dict(stages=stages,observation=str(capture/'observation.json'))
        (out/'matrix.json').write_text(json.dumps(report,indent=2)+'\n')
        print(name+': completed',flush=True)
    report['completed']=True
    (out/'matrix.json').write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--baseline',type=Path,required=True);p.add_argument('--bios',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--frames',type=int,default=1200);p.add_argument('--after-field',type=int,default=300)
    p.add_argument('--timeout',type=int,default=300)
    p.add_argument('--device-script',type=Path,help='retain and replay this input schedule in every case')
    p.add_argument('--cmake-option',action='append',default=[],help='explicit variant override, e.g. --cmake-option=-DT3_CLOSE_CAMERA=OFF')
    p.add_argument('--cases',choices=tuple(CASES),nargs='+',default=list(CASES))
    a=p.parse_args()
    if a.frames<=a.after_field or a.after_field<0 or a.timeout<=0:p.error('require frames > warmup >= 0 and positive timeout')
    if len(set(a.cases))!=len(a.cases):p.error('cases must be distinct')
    run(a)
