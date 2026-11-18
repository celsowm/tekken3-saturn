#!/usr/bin/env python3
"""Play Gon in practice mode on the headless Ymir probe and keep screenshots of his fire breath and gas (no keyboard, no window).

A pad script by program field, as ymir_practice_flow.py does: START through the opening and title, UP twice to PRACTICE MODE, START,
RIGHT 19 times on the select to Gon, START, then in the fight LP+RP (X+Y, the Dino Breath) at field 7500 and back + RK (LEFT+B, the
Gon with the Wind gas) at 7800 and 8000. The pad is the PS1's: X Square (LP), Y Triangle (RP), A Cross (LK), B Circle (RK). The fields
depend on disc read speed. Screenshots go into one sheet; the full-size frames are deleted.

    scripts/ymir_gon_fx.py --build build/saturn --out build/gon-fx
"""
import argparse
import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
BREATH, GAS = range(7520, 7648, 8), range(7810, 8100, 8)


def script():
    lines = ['0 NONE']

    def tap(at, button, length=6):
        lines.extend([f'{at} {button}', f'{at + length} NONE'])
    for at in (1700, 1760, 2250):
        tap(at, 'START')
    tap(2330, 'UP'); tap(2350, 'UP'); tap(2380, 'START')
    for k in range(19):
        tap(2900 + 30 * k, 'RIGHT')
    tap(3500, 'START')
    tap(7500, 'X+Y', 10); tap(7800, 'LEFT+B', 20); tap(8000, 'LEFT+B', 20)
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build', type=Path, default=ROOT / 'build/saturn')
    p.add_argument('--probe', type=Path, default=ROOT.parent / 'lib-saturn/harness/build-macos/probe')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    pad = args.out / 'gon.pad'
    pad.write_text(script())
    shots = list(BREATH) + list(GAS)
    command = [str(args.probe), '--iso', str(args.build / 'disc/music.cue'), '--bin', str(args.build / 'tekken3_saturn.app.bin'),
               '--bios', str(args.bios), '--frames', '8200', '--out', str(args.out / 'run.json'), '--pad-script', str(pad)]
    for field in shots:
        command += ['--screenshot', f'{field}:{args.out / f"{field}.png"}']
    subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    images = [Image.open(args.out / f'{field}.png').convert('RGB').resize((240, 168)) for field in shots]
    sheet = Image.new('RGB', (240 * 8, 168 * ((len(images) + 7) // 8)))
    for n, image in enumerate(images):
        sheet.paste(image, ((n % 8) * 240, (n // 8) * 168))
    sheet.save(args.out / 'sheet.png')
    for field in shots:
        (args.out / f'{field}.png').unlink()
    print(args.out / 'sheet.png')


if __name__ == '__main__':
    main()
