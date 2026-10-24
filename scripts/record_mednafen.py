#!/usr/bin/env python3
"""Record an actual Mednafen CD boot and retain exact build hashes privately."""
import argparse,hashlib,json,os,shutil,signal,subprocess
from pathlib import Path
import saturn_workload

def record(args):
    build=args.build.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    workload=saturn_workload.inspect(build)
    saturn_workload.validate(workload,args.workload)
    movie=out/'run.mov'
    if movie.exists():raise FileExistsError('use a new output directory to preserve the existing movie')
    command=[args.mednafen,'-sound','0','-video.driver','softfb','-ss.region_default','na',
             '-ss.bios_na_eu',str(args.bios.resolve()),'-qtrecord',str(movie),str(build/'disc/tekken3_saturn.cue')]
    env=dict(os.environ)
    neutral=args.neutral_input or args.workload in saturn_workload.RELEASE_PROFILES
    if neutral:
        profile=out/'mednafen-profile';profile.mkdir()
        env['MEDNAFEN_HOME']=str(profile)
        # Empty bindings are valid Mednafen input settings. Apply them only
        # inside this private profile, never to the user's interactive config.
        settings=[]
        for port in (1,2):
            settings += [f'-ss.input.port{port}','gamepad']
            for button in ('up','down','left','right','a','b','c','x','y','z','ls','rs','start'):
                settings += [f'-ss.input.port{port}.gamepad.{button}','']
        command[-1:-1]=settings
    if args.save_state:
        # Save the state on the SIGINT exit; scripts/mednafen_state.py reads it.
        if not neutral:raise ValueError('--save-state needs the private profile of neutral input')
        command[-1:-1]=['-autosave','1']
    if args.port2_none:
        # No pad in port 2: the build hands that fighter to the CPU.
        command[-1:-1]=['-ss.input.port2','none']
    metadata=dict(command=command,seconds_requested=args.seconds,
                  scope='Actual CD boot; inspect the recording to establish encounter coverage',
                  workload_profile=args.workload,workload=workload,neutral_input=neutral,port2_none=args.port2_none,
                  save_state=args.save_state,
                  mednafen_home=env.get('MEDNAFEN_HOME'))
    for name in ('tekken3_saturn.elf','tekken3_saturn.app.bin','disc/tekken3_saturn.bin'):
        metadata[name+'_sha256']=hashlib.sha256((build/name).read_bytes()).hexdigest()
    with (out/'mednafen.log').open('w') as log:
        process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
        (out/'pid').write_text(str(process.pid))
        try:process.wait(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            process.send_signal(signal.SIGINT);process.wait(timeout=20)
    metadata['returncode']=process.returncode
    (out/'run.json').write_text(json.dumps(metadata,indent=2)+'\n')
    if process.returncode:raise RuntimeError(f'Mednafen exited {process.returncode}')
    (out/'ffprobe.json').write_bytes(subprocess.check_output(
        ['ffprobe','-v','quiet','-count_frames','-show_streams','-of','json',str(movie)]))
    for second in args.screenshots:
        image=out/f'frame-{second}.png'
        subprocess.run(['ffmpeg','-y','-v','error','-ss',str(second),'-i',str(movie),'-frames:v','1',
                        '-vf','scale=704:480',str(image)],check=True)
        if not image.is_file():raise ValueError('no recorded video image at '+str(second)+' seconds')
    print(json.dumps(metadata,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,default=Path('build/paul-combat'))
    p.add_argument('--bios',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--mednafen',default=shutil.which('mednafen') or 'mednafen')
    p.add_argument('--workload',choices=saturn_workload.PROFILES,default='diagnostic')
    p.add_argument('--neutral-input',action='store_true',help='isolated emulator profile with both pads held neutral')
    p.add_argument('--port2-none',action='store_true',help='leave port 2 empty (CPU opponent)')
    p.add_argument('--save-state',action='store_true',help='keep the emulator state at exit (mednafen-profile/mcs)')
    p.add_argument('--seconds',type=float,default=30);p.add_argument('--screenshots',type=int,nargs='*',default=[])
    a=p.parse_args()
    if a.seconds<=0:p.error('--seconds must be positive')
    record(a)
