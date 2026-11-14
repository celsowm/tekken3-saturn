#!/usr/bin/env python3
"""Drive a built disc into practice mode on the headless Ymir probe and keep screenshots (no keyboard, no window, no recording).

The shipped disc has no scripted pad, and key injection into Mednafen steals the keyboard, so this replays a pad script by program
field (as record_ymir_video.py does): START through the opening movie and title, UP twice to PRACTICE MODE, START on the select,
then in the fight START for MODE SELECT, A for FREE, the dummy to TECH ROLL, FREEZE SIGNAL on, close, three punches, START, the
command list and back. The fields depend on disc read speed. Screenshots only; the sheet is one PNG.

    scripts/ymir_practice_flow.py --build build/saturn --out build/practice-ymir
"""
import argparse
import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHOTS = (2300, 2400, 3290, 7300, 7650, 7900, 8000, 8100, 8240, 8340, 8420, 8520, 8600, 8700)


def script():
    lines = ['0 NONE']

    def tap(at, button):
        lines.extend([f'{at} {button}', f'{at + 6} NONE'])
    for at in (1700, 1760, 2250):
        tap(at, 'START')
    tap(2330, 'UP'); tap(2350, 'UP'); tap(2380, 'START'); tap(3300, 'START')
    tap(7600, 'START'); tap(7680, 'A'); tap(7720, 'DOWN'); tap(7740, 'DOWN')
    for k in range(6):
        tap(7760 + 20 * k, 'RIGHT')
    for at in (7920, 7940, 7960):
        tap(at, 'DOWN')
    tap(7980, 'RIGHT'); tap(8020, 'START')
    for at in (8150, 8200, 8260):
        tap(at, 'X')
    tap(8300, 'START')
    for k in range(4):
        tap(8360 + 20 * k, 'UP')
    tap(8460, 'A'); tap(8560, 'DOWN'); tap(8640, 'A')
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build', type=Path, default=ROOT / 'build/saturn')
    p.add_argument('--probe', type=Path, default=ROOT.parent / 'lib-saturn/harness/build-macos/probe')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--frames', type=int, default=8800)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    pad = args.out / 'practice.pad'
    pad.write_text(script())
    command = [str(args.probe), '--iso', str(args.build / 'disc/music.cue'), '--bin', str(args.build / 'tekken3_saturn.app.bin'),
               '--bios', str(args.bios), '--frames', str(args.frames), '--out', str(args.out / 'run.json'), '--pad-script', str(pad)]
    for field in SHOTS:
        command += ['--screenshot', f'{field}:{args.out / f"{field}.png"}']
    subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
    images = [Image.open(args.out / f'{field}.png').convert('RGB').resize((320, 240)) for field in SHOTS]
    sheet = Image.new('RGB', (320 * 4, 240 * ((len(images) + 3) // 4)))
    for n, image in enumerate(images):
        sheet.paste(image, ((n % 4) * 320, (n // 4) * 240))
    sheet.save(args.out / 'sheet.png')
    for field in SHOTS:
        (args.out / f'{field}.png').unlink()
    print(args.out / 'sheet.png')


if __name__ == '__main__':
    main()
