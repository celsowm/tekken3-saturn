#!/usr/bin/env python3
"""Drive the title, mode list, select and first fight of a roster disc on Ymir.

Boots 0.BIN by injection (a cold boot, so it hands over to MENU.BIN), presses
START through the title and the mode list, waits for the select, picks both
fighters with START and keeps a screenshot of each stage. The frame numbers are
program fields and depend on disc read speed, so they are options. Screenshots
only: no recording (AGENTS.md). Needs the external Ymir probe and a BIOS.
"""
import argparse
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
STAGES = (('title', 300), ('menu', 560), ('select', 1600), ('fight', 4100))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, default=ROOT / 'build/roster')
    p.add_argument('--probe', type=Path, default=ROOT.parent / 'lib-saturn/harness/build-macos/probe')
    p.add_argument('--bios', type=Path, required=True)
    p.add_argument('--out', type=Path, default=ROOT / 'build/menu-flow')
    p.add_argument('--frames', type=int, default=4200)
    p.add_argument('--right', type=int, default=0, help='extra RIGHT presses for P1 on the select')
    p.add_argument('--title-at', type=int, default=400,
                   help='field of START on the title; the mode list is worked from 80 fields later')
    p.add_argument('--down', type=int, default=0,
                   help='DOWN presses on the mode list before choosing (5: Tekken Force)')
    p.add_argument('--series', type=int, nargs=3, metavar=('FROM', 'TO', 'STEP'),
                   help='also keep these fields and print each one\'s size and mean brightness, to time black gaps')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    script = ['0 NONE', f'{args.title_at} START', f'{args.title_at + 6} NONE']
    at = args.title_at + 80
    for _ in range(args.down):
        script += [f'{at} DOWN', f'{at + 6} NONE']
        at += 20
    script += [f'{at} START', f'{at + 6} NONE']
    at = 1500
    for _ in range(args.right):
        script += [f'{at} RIGHT', f'{at + 6} NONE']
        at += 30
    script += [f'{at + 200} START', f'{at + 206} NONE', f'{at + 260} START', f'{at + 266} NONE']
    pad = args.out / 'flow.pad'
    pad.write_text('\n'.join(script) + '\n')
    cmd = [str(args.probe), '--iso', str(args.build / 'disc/music.cue'),
           '--bin', str(args.build / 'tekken3_saturn.app.bin'), '--bios', str(args.bios),
           '--frames', str(args.frames), '--out', str(args.out / 'run.json'),
           '--pad-script', str(pad)]
    for name, frame in STAGES:
        cmd += ['--screenshot', f'{frame}:{args.out / (name + ".png")}']
    series = range(*args.series) if args.series else ()
    for frame in series:
        cmd += ['--screenshot', f'{frame}:{args.out / f"series-{frame:05}.png"}']
    code = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode
    for frame in series:
        picture = np.asarray(Image.open(args.out / f'series-{frame:05}.png').convert('L'))
        print(f'{frame} {picture.shape[1]}x{picture.shape[0]} mean {picture.mean():.1f}')
    sys.exit(code)


if __name__ == '__main__':
    main()
