#!/usr/bin/env python3
"""Run a private retail CUE through the sibling harness, retaining provenance."""
import argparse,datetime,hashlib,json,re,shutil,subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def digest(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda:f.read(1024*1024),b''):h.update(block)
    return h.hexdigest()

def run(a):
    disc=a.disc.resolve();out=a.out.resolve();probe=a.probe.resolve();bios=a.bios.resolve()
    if out.exists() and any(out.iterdir()):raise FileExistsError('use a fresh empty output directory')
    tracks=re.findall(r'^\s*FILE\s+"([^"]+)"\s+\w+',disc.read_text(),re.M|re.I)
    if not tracks:raise ValueError('reference capture requires a CUE with quoted FILE entries')
    provenance=dict(disc_cue_sha256=digest(disc),bios_sha256=digest(bios),probe_sha256=digest(probe),
                    tracks={name:digest(disc.parent/name) for name in tracks})
    out.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(a.input_script,out/'input.pad')
    command=[str(probe),'--iso',str(disc),'--bios',str(bios),'--boot-mode','bios',
             '--sh2-cache',a.sh2_cache,'--frames',str(a.frames),
             '--pad-script',str(out/'input.pad'),'--backup-ram',str(out/'backup.bin'),
             '--out',str(out/'ymir.json')]
    if not a.no_trace:
        command+=['--debug-profile',str(out/'debug'),'--trace-from',str(a.trace_from),
                  '--trace-frames',str(a.trace_frames),'--trace-capacity',str(a.trace_capacity)]
        for pc in a.snapshot_pc:command+=['--snapshot-pc',hex(pc)]
    for frame in a.screenshots:command+=['--screenshot',f'{frame}:{out / ("screen-"+str(frame)+".png")}']
    metadata=dict(version=1,command=command,provenance=provenance,
                  input_sha256=digest(out/'input.pad'),
                  started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  scope='Real BIOS boot of private retail disc; inspect screenshots and traces to establish scene and cadence. No guest executable or fighter state injection.')
    for name,path in [('project',ROOT),('libsaturn',a.libsaturn)]:
        metadata[name+'_revision']=subprocess.check_output(['git','-C',str(path),'describe','--always','--dirty'],text=True).strip()
    (out/'reference.json').write_text(json.dumps(metadata,indent=2)+'\n')
    try:
        with (out/'probe.log').open('w') as log:
            result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT,timeout=a.timeout)
    except subprocess.TimeoutExpired:
        metadata['failure']='probe timeout'
        (out/'reference.json').write_text(json.dumps(metadata,indent=2)+'\n')
        raise
    metadata['returncode']=result.returncode
    (out/'reference.json').write_text(json.dumps(metadata,indent=2)+'\n')
    result.check_returncode()
    if not a.no_trace:
        profile=json.loads((out/'debug.json').read_text())
        if profile.get('truncated',True):raise ValueError('retail reference trace truncated')
        if profile.get('frames_stored')!=a.trace_frames:
            raise ValueError('retail reference trace window incomplete')
        if a.snapshot_pc:
            from analyze_saturn_profile import disassemble_snapshots
            disassemble_snapshots(out/'debug',a.objdump)
    print(json.dumps(metadata,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc',required=True,type=Path);p.add_argument('--bios',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path)
    p.add_argument('--libsaturn',type=Path,default=ROOT.parent/'lib-saturn')
    p.add_argument('--probe',type=Path,default=ROOT.parent/'lib-saturn/harness/build-macos/probe')
    p.add_argument('--input-script',type=Path,default=ROOT/'scripts/inputs/vf2_usa_reference.pad')
    p.add_argument('--sh2-cache',choices=('on','off'),default='on')
    p.add_argument('--frames',type=int,default=5000);p.add_argument('--timeout',type=int,default=600)
    p.add_argument('--trace-from',type=int,default=4800);p.add_argument('--trace-frames',type=int,default=120)
    p.add_argument('--trace-capacity',type=int,default=1048576)
    p.add_argument('--snapshot-pc',action='append',type=lambda x:int(x,0),default=[])
    p.add_argument('--objdump',default=str(ROOT.parent/'saturn-tools/bin/sh2eb-elf-objdump'))
    p.add_argument('--screenshots',type=int,nargs='*',default=[3599,4799,4919])
    p.add_argument('--no-trace',action='store_true')
    a=p.parse_args()
    if a.frames<=0 or a.timeout<=0:p.error('frames and timeout must be positive')
    if any(f<0 or f>=a.frames for f in a.screenshots):p.error('screenshots must be inside run')
    if not a.no_trace and (a.trace_from<0 or a.trace_frames<=0 or a.trace_from+a.trace_frames>a.frames):p.error('trace window must fit inside run')
    if not 1<=a.trace_capacity<=1048576:p.error('trace capacity must be 1..1048576')
    if a.no_trace and a.snapshot_pc:p.error('snapshot PCs require tracing')
    run(a)
