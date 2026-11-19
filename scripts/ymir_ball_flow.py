#!/usr/bin/env python3
"""Drive a built disc into Tekken Ball on the headless Ymir probe and keep screenshots (no keyboard, no window, no recording).

The shipped disc has no scripted pad, so this replays a pad script by program field (as ymir_practice_flow.py does): START
through the opening movie and title, DOWN five times to TEKKEN BALL, START, START on the single-player select, LEFT for each step
of the ball type and START to confirm it, then the fight: a punch in the court and screenshots of the serve, the rally and the HUD.
The fields depend on disc read
speed. Screenshots only; the sheet is one PNG.

    scripts/ymir_ball_flow.py --build build/saturn --out build/ball-ymir
"""
import argparse
import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHOTS = (2300, 2440, 2600, 3300, 3700, 4300, 5000, 5600, 6200, 6800, 7400, 8000)


def script(select=3300, ball_type=0, pause_at=0, pause_item=0, right=0):
    lines = ['0 NONE']

    def tap(at, button, length=6):
        lines.extend([f'{at} {button}', f'{at + length} NONE'])
    for at in (1700, 1760, 2250):
        tap(at, 'START')
    for k in range(5):
        tap(2330 + 20 * k, 'DOWN')
    tap(2460, 'START')                       # TEKKEN BALL
    for k in range(right):                   # move the select cursor first
        tap(select - 150 + 30 * k, 'RIGHT')
    tap(select, 'START')                     # the single-player select: the fighter, then the ball type
    for k in range(ball_type):               # LEFT rolls the next type in (it rests about 30 fields later)
        tap(select + 40 + 40 * k, 'LEFT')
    confirm = select + 80 + 40 * ball_type
    tap(confirm, 'START'); tap(confirm + 120, 'START')
    for k in range(30):                      # in the fight: walk forward and punch now and then (X is also the menu's OK: none once paused)
        if not pause_at or 5200 + 150 * k < pause_at:
            tap(5200 + 150 * k, 'X')
    if pause_at:
        tap(pause_at, 'START')
        for k in range(pause_item):          # DOWN to the item: 1 COMMAND, 2 RESET, 3 HOW TO (a button closes a page; the next one is CANCEL)
            tap(pause_at + 30 + 40 * k, 'DOWN', 14)
        tap(pause_at + 70 + 40 * pause_item, 'A', 14)
        if pause_item in (1, 3):
            # The first HOW TO visit streams its picture from disc before accepting input.
            tap(pause_at + 420, 'A')      # close the page and return to the selected pause row
            tap(pause_at + 480, 'START')  # resume the fight from that row
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build', type=Path, default=ROOT / 'build/saturn')
    p.add_argument('--probe', type=Path, default=ROOT.parent / 'lib-saturn/harness/build-macos/probe')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--frames', type=int, default=8100)
    p.add_argument('--shots', type=int, nargs='*', default=list(SHOTS))
    p.add_argument('--select', type=int, default=3300)
    p.add_argument('--ball-type', type=int, default=0)
    p.add_argument('--right', type=int, default=0, help='press RIGHT this many times on the select screen first (another fighter)')
    p.add_argument('--pause-at', type=int, default=0, help='press START in the fight at this field (the pause menu)')
    p.add_argument('--pause-item', type=int, default=0, help='the pause menu item to choose: 0 CANCEL, 1 COMMAND, 2 RESET, 3 HOW TO')
    p.add_argument('--keep', action='store_true', help='keep the full-size screenshots next to the sheet')
    p.add_argument('--probe-arg', action='append', default=[], help='an extra probe argument (repeat; e.g. --probe-arg=--watch-write=ADDR:4)')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    pad = args.out / 'ball.pad'
    pad.write_text(script(args.select, args.ball_type, args.pause_at, args.pause_item, args.right))
    command = [str(args.probe), '--iso', str(args.build / 'disc/music.cue'), '--bin', str(args.build / 'tekken3_saturn.app.bin'),
               '--bios', str(args.bios), '--frames', str(args.frames), '--out', str(args.out / 'run.json'), '--pad-script', str(pad)]
    for field in args.shots:
        command += ['--screenshot', f'{field}:{args.out / f"{field}.png"}']
    for extra in args.probe_arg:
        command += extra.split('=', 1) if extra.startswith('--') and '=' in extra else [extra]
    with open(args.out / 'probe.log', 'w') as log:
        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
    images = [Image.open(args.out / f'{field}.png').convert('RGB').resize((320, 240)) for field in args.shots]
    sheet = Image.new('RGB', (320 * 4, 240 * ((len(images) + 3) // 4)))
    for n, image in enumerate(images):
        sheet.paste(image, ((n % 4) * 320, (n // 4) * 240))
    sheet.save(args.out / 'sheet.png')
    if not args.keep:
        for field in args.shots:
            (args.out / f'{field}.png').unlink()
    print(args.out / 'sheet.png')


if __name__ == '__main__':
    main()
