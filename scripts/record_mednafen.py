#!/usr/bin/env python3
"""Record an actual Mednafen CD boot and retain exact build hashes privately."""
import argparse,hashlib,json,os,re,shutil,signal,subprocess
from pathlib import Path
import saturn_workload

def record(args):
    build=args.build.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    start_after=getattr(args,'start_after',None)
    workload=saturn_workload.inspect(build)
    saturn_workload.validate(workload,args.workload)
    movie=out/'run.mov'
    if movie.exists():raise FileExistsError('use a new output directory to preserve the existing movie')
    cue=(args.cue or build/'disc/tekken3_saturn.cue').resolve()
    command=[args.mednafen,'-sound','1' if args.audio else '0','-video.driver','softfb','-ss.region_default','na',
             '-ss.bios_na_eu',str(args.bios.resolve()),'-qtrecord',str(movie),str(cue)]
    env=dict(os.environ)
    neutral=args.neutral_input or args.workload in saturn_workload.RELEASE_PROFILES
    if getattr(args,'backup_from',None) and not neutral:raise ValueError('--backup-from needs an isolated neutral-input profile')
    if getattr(args,'backup_from',None) and getattr(args,'resume_from',None):raise ValueError('choose a cold backup boot or a saved-state resume')
    backup_files=[]
    if neutral:
        profile=out/'mednafen-profile';profile.mkdir()
        env['MEDNAFEN_HOME']=str(profile)
        backup_from=getattr(args,'backup_from',None)
        if backup_from is not None:
            (profile/'sav').mkdir()
            for extension in ('bkr','bcr'):
                found=list((backup_from/'mednafen-profile/sav').glob('*.'+extension))
                if len(found)!=1:raise ValueError('--backup-from needs one '+extension+' backup RAM file')
                source=found[0]
                shutil.copy2(source,profile/'sav'/('force-backup.'+extension))
                backup_files.append(dict(source=str(source.resolve()),sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
            # Backup RAM is hardware state, independent of the program image.
            # A fixed name in this isolated profile also supports a new build.
            command[-1:-1]=['-filesys.fname_sav','force-backup.%x']
        resume_from=getattr(args,'resume_from',None)
        if resume_from is not None:
            states=list((resume_from/'mednafen-profile/mcs').glob('*.mca'))
            if len(states)!=1:raise ValueError('--resume-from needs exactly one saved emulator state')
            (profile/'mcs').mkdir()
            shutil.copy2(states[0],profile/'mcs'/states[0].name)
        # Empty bindings are valid Mednafen input settings. Apply them only
        # inside this private profile, never to the user's interactive config.
        settings=[]
        for port in (1,2):
            settings += [f'-ss.input.port{port}','gamepad']
            for button in ('up','down','left','right','a','b','c','x','y','z','ls','rs','start'):
                settings += [f'-ss.input.port{port}.gamepad.{button}','']
        if start_after is not None:
            # SDL scancode 40 is Return; the helper sends macOS keycode 36.
            settings += ['-ss.input.port1.gamepad.start','keyboard 0x0 40']
        if getattr(args,'input_script',None):
            settings += ['-ss.input.port1.gamepad.start','keyboard 0x0 40',
                         '-ss.input.port1.gamepad.right','keyboard 0x0 79',
                         # Down arrow (SDL 81, macOS keycode 125) walks the menus.
                         '-ss.input.port1.gamepad.down','keyboard 0x0 81',
                         '-ss.input.port1.gamepad.x','keyboard 0x0 4',
                         '-ss.input.port1.gamepad.a','keyboard 0x0 29']
        command[-1:-1]=settings
    if args.save_state or getattr(args,'resume_from',None):
        # Save the state on the SIGINT exit; scripts/mednafen_state.py reads it.
        if not neutral:raise ValueError('--save-state needs the private profile of neutral input')
        command[-1:-1]=['-autosave','1']
    if args.port2_none:
        # No pad in port 2: the build hands that fighter to the CPU.
        command[-1:-1]=['-ss.input.port2','none']
    metadata=dict(command=command,seconds_requested=args.seconds,
                  scope='Actual CD boot; inspect the recording to establish encounter coverage',
                  workload_profile=args.workload,workload=workload,neutral_input=neutral,port2_none=args.port2_none,
                  save_state=args.save_state,audio=args.audio,start_after=start_after,
                  resume_from=str(args.resume_from.resolve()) if getattr(args,'resume_from',None) else None,
                  backup_from=str(args.backup_from.resolve()) if getattr(args,'backup_from',None) else None,
                  backup_files=backup_files,
                  cue_sha256=hashlib.sha256(cue.read_bytes()).hexdigest(),
                  mednafen_home=env.get('MEDNAFEN_HOME'))
    cue_files=re.findall(r'^\s*FILE\s+"([^"]+)"\s+BINARY\s*$',cue.read_text(),re.IGNORECASE|re.MULTILINE)
    if not cue_files:raise ValueError('CUE has no quoted binary files')
    metadata['cue_files']=[dict(path=str((cue.parent/name).resolve()),
                                sha256=hashlib.sha256((cue.parent/name).read_bytes()).hexdigest())
                           for name in cue_files]
    program=getattr(args,'program','tekken3_saturn')
    for name in (f'{program}.elf',f'{program}.app.bin',f'disc/{program}.bin'):
        metadata[name+'_sha256']=hashlib.sha256((build/name).read_bytes()).hexdigest()
    prior_run=getattr(args,'resume_from',None)
    if prior_run:
        prior=json.loads((prior_run/'run.json').read_text())
        key=f'{program}.elf_sha256'
        if prior.get(key)!=metadata[key] or [q['sha256'] for q in prior.get('cue_files',[])]!=[q['sha256'] for q in metadata['cue_files']]:
            raise ValueError('saved-state/backup reuse requires the same executable and disc image')
    with (out/'mednafen.log').open('w') as log:
        process=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT)
        (out/'pid').write_text(str(process.pid))
        keypress=None
        scripted=None
        if start_after is not None:
            keypress=subprocess.Popen(['swift',str(Path(__file__).with_name('mednafen_press_start.swift')),
                                       str(process.pid),str(start_after),'0.25'],
                                      stdout=log,stderr=subprocess.STDOUT)
        if getattr(args,'input_script',None):
            scripted=subprocess.Popen(['swift',str(Path(__file__).with_name('mednafen_input_script.swift')),
                                       str(process.pid),str(args.input_script.resolve())],
                                      stdout=log,stderr=subprocess.STDOUT)
        try:process.wait(timeout=args.seconds)
        except subprocess.TimeoutExpired:
            process.send_signal(signal.SIGINT);process.wait(timeout=20)
        if keypress is not None and keypress.wait(timeout=10):
            raise RuntimeError('automated START helper failed; inspect mednafen.log')
        if scripted is not None and scripted.wait(timeout=10):
            raise RuntimeError('automated input helper failed; inspect mednafen.log')
    metadata['returncode']=process.returncode
    (out/'run.json').write_text(json.dumps(metadata,indent=2)+'\n')
    if process.returncode:raise RuntimeError(f'Mednafen exited {process.returncode}')
    (out/'ffprobe.json').write_bytes(subprocess.check_output(
        ['ffprobe','-v','quiet','-count_frames','-show_streams','-of','json',str(movie)]))
    for second in args.screenshots:
        image=out/f'frame-{second:g}.png'
        subprocess.run(['ffmpeg','-y','-v','error','-ss',str(second),'-i',str(movie),'-frames:v','1',
                        '-vf','scale=704:480',str(image)],check=True)
        if not image.is_file():raise ValueError('no recorded video image at '+str(second)+' seconds')
    print(json.dumps(metadata,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,default=Path('build/paul-combat'))
    p.add_argument('--program',default='tekken3_saturn',help='executable basename whose build hashes are recorded')
    p.add_argument('--cue',type=Path,help='alternate CUE, for example a mixed-mode audio disc')
    p.add_argument('--bios',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--mednafen',default=shutil.which('mednafen') or 'mednafen')
    p.add_argument('--workload',choices=saturn_workload.PROFILES,default='diagnostic')
    p.add_argument('--neutral-input',action='store_true',help='isolated emulator profile with both pads held neutral')
    p.add_argument('--port2-none',action='store_true',help='leave port 2 empty (CPU opponent)')
    p.add_argument('--backup-from',type=Path,help='cold boot with backup RAM copied from a previous isolated recording')
    p.add_argument('--resume-from',type=Path,help='resume one saved state from a previous isolated recording of this disc')
    p.add_argument('--save-state',action='store_true',help='keep the emulator state at exit (mednafen-profile/mcs)')
    p.add_argument('--audio',action='store_true',help='enable emulator audio for an audible playback check')
    p.add_argument('--start-after',type=float,help='on macOS, send START after this many seconds using an isolated profile')
    p.add_argument('--input-script',type=Path,help='macOS key events script: seconds, virtual key code, hold seconds')
    p.add_argument('--seconds',type=float,default=30);p.add_argument('--screenshots',type=float,nargs='*',default=[])
    a=p.parse_args()
    if a.seconds<=0:p.error('--seconds must be positive')
    if a.start_after is not None and (a.start_after<0 or a.start_after+1>=a.seconds):p.error('START must occur at least one second before the end')
    if a.start_after is not None and not (a.neutral_input or a.workload in saturn_workload.RELEASE_PROFILES):p.error('--start-after requires an isolated neutral-input profile')
    if a.input_script is not None and not (a.neutral_input or a.workload in saturn_workload.RELEASE_PROFILES):p.error('--input-script requires an isolated neutral-input profile')
    if any(second<0 or second>=a.seconds for second in a.screenshots):p.error('screenshot times must be within the recording')
    record(a)
