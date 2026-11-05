#!/usr/bin/env python3
"""Run a one-player arcade ladder on Ymir and keep a screenshot every few seconds.

Use a build configured with -DT3_LADDER_SELFTEST=ON: each fight is won on its
own after 240 fields and fight 10 hands over to True Ogre, so one run covers
the ten VS screens (STAGE 1 .. 10), True Ogre and the closing screen. On an
ordinary build the pad stays idle after the pick, so the CPU wins and the run
shows CONTINUE? and GAME OVER. --challenger FIELD presses START on port 2 at
that field (A NEW CHALLENGER), then both players pick. Screenshots, the observation JSON
and logs stay; nothing else is recorded. This checks injected execution in an
emulator, never real-hardware timing.
"""
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from hw_report_check import INTRO, SKIP_INTRO, device_script  # noqa: E402

# Title START, ARCADE MODE (the first row), then the pick on the select screen.
TO_LADDER = SKIP_INTRO + [(INTRO + 400, 'START'), (INTRO + 480, 'START'),
                          (INTRO + 2100, 'START'), (INTRO + 2160, 'START')]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build', type=Path, default=ROOT / 'build/ladder-test')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--libsaturn', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--frames', type=int, default=22000)
    p.add_argument('--every', type=int, default=400, help='fields between screenshots')
    p.add_argument('--timeout', type=int, default=3000)
    p.add_argument('--port2', choices=('pad', 'none'), default='pad',
                   help='a second pad must not turn the arcade ladder into a versus match')
    p.add_argument('--challenger', type=int, help='field of port 2 START, inside a ladder fight')
    p.add_argument('--challenger-attacks', action='store_true',
                   help='port 2 walks in and attacks through the challenge match, so it wins and carries the run on')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    script = args.out / 'ladder.devices'
    lines = device_script(TO_LADDER)
    if args.challenger:
        # START on port 2, then both picks on the select screen that follows.
        for at, port in [(args.challenger, 2), (args.challenger + 1700, 1), (args.challenger + 1700, 2)]:
            lines += f'{at} {port} buttons=START\n{at + 6} {port} buttons=NONE\n'
        if args.challenger_attacks:
            for at in range(args.challenger + 2600, args.frames, 24):
                # Port 2 starts on the right: LEFT walks toward port 1, then a low kick, which an idle
                # standing guard does not stop.
                lines += f'{at} 2 buttons=LEFT\n{at + 12} 2 buttons=DOWN+A\n'
    script.write_text(''.join(sorted(lines.splitlines(True), key=lambda line: int(line.split()[0]))))
    shots = []
    for field in range(INTRO + 2200, args.frames - 1, args.every):
        shots += ['--screenshot-frame', str(field)]
    command = [sys.executable, str(ROOT / 'scripts/saturn_probe.py'), '--build', str(args.build),
               '--disc', str(args.build / 'disc/music.cue'), '--bios', str(args.bios),
               '--libsaturn', str(args.libsaturn), '--out', str(args.out / 'run'),
               '--frames', str(args.frames), '--timeout', str(args.timeout),
               '--port2', args.port2, '--device-script', str(script)] + shots
    sys.exit(subprocess.run(command).returncode)


if __name__ == '__main__':
    main()
