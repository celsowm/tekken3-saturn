#!/usr/bin/env python3
"""Play a disc in Mednafen with timed key presses and keep its own screenshots.

Mednafen saves a PNG per F9 into its snaps folder. This starts the emulator,
sends the steps below to it through System Events (the first run asks macOS for
accessibility permission), and copies each new snapshot into --out under the
step's name. It deletes nothing in Mednafen's folders, and it only types into
the emulator's window, which it brings forward before each key.

Snapshots work. Key presses have not been seen to reach the game: Return at the
title did nothing, so check the screenshots before trusting a key step.

A step is `SECONDS:ACTION[:NAME]`, seconds counted from launch:
  snap:NAME   take a screenshot, kept as NAME.png
  key:NAME    press a key: return, up, down, left, right, space, or a number (key code)

    scripts/mednafen_shots.py build/roster/disc/music.cue --out build/mn-shots \\
        --step 9:snap:namco --step 24:key:return --step 26:snap:menu
"""
import argparse
import shutil
import subprocess
import time
from pathlib import Path

KEYS = {'return': 36, 'space': 49, 'left': 123, 'right': 124, 'down': 125, 'up': 126, 'f9': 101}


def press(code):
    # bring the emulator forward: key codes go to the frontmost application
    subprocess.run(['osascript', '-e', 'tell application "System Events" to set frontmost of first process whose name is "mednafen" to true'], check=False)
    subprocess.run(['osascript', '-e', f'tell application "System Events" to key code {code}'], check=True)


def snaps(folder):
    return {p: p.stat().st_mtime for p in folder.glob('*.png')}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('disc', type=Path)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--step', action='append', default=[], metavar='SECONDS:ACTION[:NAME]')
    p.add_argument('--mednafen', default='mednafen')
    p.add_argument('--snaps', type=Path, default=Path.home() / '.mednafen/snaps')
    p.add_argument('--mednafen-arg', action='append', default=[], help='extra emulator option, e.g. --mednafen-arg=-ss.input.port2 --mednafen-arg=none')
    p.add_argument('--keep-open', action='store_true', help='leave the emulator running afterwards')
    args = p.parse_args()
    steps = []
    for text in args.step:
        seconds, action, *name = text.split(':')
        steps.append((float(seconds), action, name[0] if name else action))
    args.out.mkdir(parents=True, exist_ok=True)
    emulator = subprocess.Popen([args.mednafen, '-sound', '0', *args.mednafen_arg, str(args.disc.resolve())],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    started = time.time()
    try:
        for seconds, action, name in sorted(steps):
            time.sleep(max(0.0, started + seconds - time.time()))
            if action == 'key':
                press(KEYS.get(name, name))
            elif action == 'snap':
                before = snaps(args.snaps)
                press(KEYS['f9'])
                time.sleep(1.5)
                new = [q for q, t in snaps(args.snaps).items() if q not in before]
                if not new:
                    raise SystemExit(f'no snapshot appeared for {name}')
                shutil.copy(max(new, key=lambda q: q.stat().st_mtime), args.out / f'{name}.png')
                print('saved', name)
            else:
                raise SystemExit(f'unknown action {action}')
    finally:
        if not args.keep_open:
            emulator.terminate()


if __name__ == '__main__':
    main()
