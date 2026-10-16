#!/usr/bin/env python3
"""Record an actual Mednafen CD boot and retain exact build hashes privately."""
import argparse,hashlib,json,shutil,signal,subprocess
from pathlib import Path

def record(args):
    build=args.build.resolve();out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    movie=out/'run.mov'
    if movie.exists():raise FileExistsError('use a new output directory to preserve the existing movie')
    command=[args.mednafen,'-sound','0','-video.driver','softfb','-ss.region_default','na',
             '-ss.bios_na_eu',str(args.bios.resolve()),'-qtrecord',str(movie),str(build/'disc/tekken3_saturn.cue')]
    metadata=dict(command=command,seconds_requested=args.seconds,
                  scope='Actual CD boot; inspect the recording to establish encounter coverage')
    for name in ('tekken3_saturn.elf','tekken3_saturn.app.bin','disc/tekken3_saturn.bin'):
        metadata[name+'_sha256']=hashlib.sha256((build/name).read_bytes()).hexdigest()
    with (out/'mednafen.log').open('w') as log:
        process=subprocess.Popen(command,stdout=log,stderr=subprocess.STDOUT)
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
        subprocess.run(['ffmpeg','-y','-v','error','-ss',str(second),'-i',str(movie),'-frames:v','1',
                        '-vf','scale=704:480',str(out/f'frame-{second}.png')],check=True)
    print(json.dumps(metadata,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',type=Path,default=Path('build/paul-combat'))
    p.add_argument('--bios',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--mednafen',default=shutil.which('mednafen') or 'mednafen')
    p.add_argument('--seconds',type=float,default=30);p.add_argument('--screenshots',type=int,nargs='*',default=[])
    a=p.parse_args()
    if a.seconds<=0:p.error('--seconds must be positive')
    record(a)
