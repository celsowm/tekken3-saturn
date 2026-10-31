#!/usr/bin/env python3
"""Check the hardware report and the music switch of a roster disc on Ymir.

Runs three scenarios on the external Ymir probe and prints their pacing:
  plain      no input, the attract fight (compare with --control, an earlier run)
  debug      OPTION MODE: DEBUG on, then a fight
  music-off  OPTION MODE: DEBUG on and MUSIC off, then a fight

Each keeps screenshots and the level of the SCSP output at the end of the run,
which falls silent between effects only when the music is off. The audio
capture is deleted after it is measured. This checks injected execution in an
emulator, never real-hardware timing (docs/REAL_HARDWARE.md).
"""
import argparse
import json
import math
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KEYS = ('frame_serial', 'vblanks', 'sim_ticks', 'loop_rate_x10', 'observed_fields',
        'discarded_fields', 'commands_used', 'faces_flushed', 'first_error')
# Title, then the mode list: UP wraps to OPTION MODE, whose first row is DEBUG.
TO_OPTIONS = [(400, 'START'), (480, 'UP'), (510, 'START')]
DEBUG_ON = [(540, 'LEFT')]
MUSIC_OFF = [(570, 'DOWN'), (600, 'LEFT')]
# Back out, DOWN wraps to ARCADE MODE, then both picks on the select screen.
TO_FIGHT = [(640, 'B'), (670, 'DOWN'), (700, 'START'), (2100, 'START'), (2160, 'START'), (2400, 'START')]
SCENARIOS = {
    'plain': [],
    'debug': TO_OPTIONS + DEBUG_ON + TO_FIGHT,
    'music-off': TO_OPTIONS + DEBUG_ON + MUSIC_OFF + TO_FIGHT,
}


def device_script(presses):
    lines = ['0 1 buttons=NONE', '0 2 buttons=NONE']
    for frame, button in presses:
        lines += [f'{frame} 1 buttons={button}', f'{frame + 6} 1 buttons=NONE']
    return '\n'.join(lines) + '\n'


def levels(path, tail_seconds=8.0, seconds=0.5, rate=44100):
    """The quietest and loudest RMS of half-second windows over the capture's end: the fight."""
    data = path.read_bytes()
    count = len(data) // 2
    samples = struct.unpack(f'<{count}h', data[:count * 2])
    window = int(seconds * rate * 2)
    first = max(0, count - int(tail_seconds * rate * 2))
    rms = [math.sqrt(sum(v * v for v in samples[at:at + window]) / window)
           for at in range(first, count - window + 1, window)]
    return {'quietest_rms': round(min(rms), 1), 'loudest_rms': round(max(rms), 1)} if rms else {}


def pacing(run):
    state = json.loads((run / 'observation.json').read_text())['state']
    return {key: state.get(key) for key in KEYS}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--build', type=Path, default=ROOT / 'build/roster')
    p.add_argument('--bios', type=Path, required=True)
    p.add_argument('--libsaturn', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--frames', type=int, default=4800)
    p.add_argument('--timeout', type=int, default=500)
    p.add_argument('--control', type=Path, help='an earlier saturn_probe.py run of the plain scenario')
    p.add_argument('--scenario', action='append', choices=SCENARIOS)
    args = p.parse_args()
    report = {}
    if args.control:
        report['control'] = pacing(args.control)
    for name in args.scenario or SCENARIOS:
        folder = args.out / name
        folder.mkdir(parents=True, exist_ok=True)
        script = folder / 'flow.devices'
        script.write_text(device_script(SCENARIOS[name]))
        run = folder / 'run'
        result = subprocess.run(
            [sys.executable, str(ROOT / 'scripts/saturn_probe.py'), '--build', str(args.build),
             '--disc', str(args.build / 'disc/music.cue'), '--bios', str(args.bios),
             '--libsaturn', str(args.libsaturn), '--out', str(run), '--frames', str(args.frames),
             '--timeout', str(args.timeout), '--device-script', str(script),
             '--screenshot-frame', '630', '--screenshot-frame', str(args.frames - 100), '--capture-audio'],
            capture_output=True, text=True)
        if result.returncode:
            report[name] = {'error': result.returncode, 'stderr': result.stderr[-2000:]}
            continue
        report[name] = pacing(run)
        for capture in run.glob('*.s16le'):
            report[name].update(levels(capture))
            capture.unlink()
    print(json.dumps(report, indent=2))
    (args.out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
