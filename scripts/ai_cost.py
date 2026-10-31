#!/usr/bin/env python3
"""Measure CPU move selection on Ymir, and hash its pads to check a speed-up.

Runs two fights on the external Ymir probe with cache emulation on:
  attract  no input, the attract fight: two CPU fighters
  arcade   ARCADE MODE, no second pad, P1 neutral against one CPU fighter

An AI speed-up must leave `pad_hash` unchanged (the pads of the first 2,000
updates). The costs are emulator timing, not hardware timing.
"""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
# Title, the mode list (ARCADE MODE is first), then both picks on the select screen.
TO_FIGHT = [(400, 'START'), (700, 'START'), (2100, 'START'), (2160, 'START'), (2400, 'START')]
SCENARIOS = {'attract': [], 'arcade': TO_FIGHT}
FRT_PER_FIELD = 3728


def device_script(presses):
    lines = ['0 1 buttons=NONE']
    for frame, button in presses:
        lines += [f'{frame} 1 buttons={button}', f'{frame + 6} 1 buttons=NONE']
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, default=ROOT / 'build/roster')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--libsaturn', type=Path, default=Path.home() / 'dev/lib-saturn')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--frames', type=int, default=6000)
    p.add_argument('--timeout', type=int, default=600)
    p.add_argument('--scenario', action='append', choices=SCENARIOS)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    report = {}
    for name in args.scenario or SCENARIOS:
        folder = args.out / name
        folder.mkdir(exist_ok=True)
        script = folder / 'flow.devices'
        script.write_text(device_script(SCENARIOS[name]))
        run = folder / 'run'
        shutil.rmtree(run, ignore_errors=True)
        subprocess.run([sys.executable, str(ROOT / 'scripts/saturn_probe.py'), '--build', str(args.build),
                        '--disc', str(args.build / 'disc/music.cue'), '--bios', str(args.bios),
                        '--libsaturn', str(args.libsaturn), '--out', str(run), '--frames', str(args.frames),
                        '--timeout', str(args.timeout), '--device-script', str(script), '--sh2-cache', 'on',
                        '--port2', 'none',
                        '--screenshot-frame', str(args.frames - 100)],
                       stdout=subprocess.DEVNULL, check=True)
        obs = json.loads((run / 'observation.json').read_text())
        opt, state = obs['optimizations'], obs.get('state', obs)
        calls = opt['ai_calls']
        report[name] = dict(
            pad_hash=f"{opt['ai_pad_hash']:#010x}", ai_calls=calls,
            ai_mean_field=round(opt['ai_ticks'] / calls / FRT_PER_FIELD, 3) if calls else 0,
            ai_max_field=round(opt['ai_max_ticks'] / FRT_PER_FIELD, 3),
            ai_reads_per_call=round(opt['ai_reads'] / calls) if calls else 0,
            **{k: state.get(k) for k in ('frame_serial', 'observed_fields', 'discarded_fields', 'sim_ticks',
                                         'first_error')})
    (args.out / 'ai-cost.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report, indent=1))


if __name__ == '__main__':
    main()
