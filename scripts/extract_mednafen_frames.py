#!/usr/bin/env python3
"""Inspect an existing Mednafen recording without rebooting or replacing evidence."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess


def extract(movie,out,seconds):
    if not movie.is_file():raise FileNotFoundError(movie)
    if not seconds or any(not math.isfinite(v) or v<0 for v in seconds):
        raise ValueError('require nonnegative finite screenshot times')
    if out.exists() and any(out.iterdir()):raise FileExistsError('use a fresh output directory')
    out.mkdir(parents=True,exist_ok=True)
    report=dict(movie=str(movie.resolve()),movie_sha256=hashlib.sha256(movie.read_bytes()).hexdigest(),frames=[])
    for second in sorted(set(seconds)):
        image=out/f'frame-{second:g}.png'
        command=['ffmpeg','-v','error','-ss',str(second),'-i',str(movie.resolve()),
                 '-frames:v','1','-vf','scale=704:480',str(image)]
        subprocess.run(command,check=True)
        if not image.is_file():raise ValueError('no video image at '+str(second)+' seconds')
        report['frames'].append(dict(second=second,file=image.name,command=command))
    (out/'frames.json').write_text(json.dumps(report,indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--movie',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--seconds',type=float,nargs='+',required=True)
    a=p.parse_args();extract(a.movie,a.out,a.seconds)
