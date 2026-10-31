#!/usr/bin/env python3
"""Boot fighter pairs from one --boot-fighters build and keep each result.

Reconfigures the build in place, so no variant builds pile up (AGENTS.md disk
budget). Each pair leaves two screenshots and a JSON summary under --out; no
recordings. The default pairs start every fighter of the roster at least once
on each side, then the mirror match and the worst cases for sound and memory.
"""
import argparse
import json
import subprocess
from pathlib import Path

import fighter_roster

WORST = ((5, 14), (14, 5), (3, 8), (0, 0), (15, 15), (20, 20))


def default_pairs():
    count = len(fighter_roster.ROSTER)
    return [(i, (i + 1) % count) for i in range(count)] + list(WORST)


def summary(report):
    state, optimizations = report['state'], report['optimizations']
    return dict(sim_ticks=state['sim_ticks'], submitted_frames=state['submitted_frames'],
                observed_fields=state['observed_fields'],
                pose_status=state['psx_pose_status'], event_status=state['psx_event_status'],
                combat=state['psx_combat_active'], faces=state['faces_submitted'],
                sfx_played=optimizations.get('sfx_played'),
                event_overflow=optimizations.get('event_overflow'),
                slave_stack_used=optimizations.get('slave_sim_stack_used'))


def run_pair(args, first, second):
    out = args.out / f'{first:02d}-{second:02d}'
    result_path = out.with_suffix('.json')
    if args.resume and result_path.is_file():
        return json.loads(result_path.read_text())
    subprocess.run(['cmake', '.', f'-DT3_BOOT_FIGHTERS={first};{second}'],
                   cwd=args.build, check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['ninja'], cwd=args.build, check=True, stdout=subprocess.DEVNULL)
    command = ['python3', str(Path(__file__).with_name('saturn_probe.py')),
               '--build', str(args.build), '--disc', str(args.build / 'disc/music.cue'),
               '--libsaturn', str(args.libsaturn), '--bios', str(args.bios), '--nm', args.nm,
               '--out', str(out), '--frames', str(args.frames), '--timeout', str(args.timeout),
               '--screenshot-frame', str(args.frames // 2), '--port2', args.port2]
    done = subprocess.run(command, capture_output=True, text=True)
    if done.returncode:
        result = dict(pair=[first, second], ok=False, error=done.stderr.strip().splitlines()[-1])
    else:
        result = dict(pair=[first, second], ok=True, **summary(json.loads(done.stdout)))
    # Only the pictures and the summary stay; the raw dumps are large and repeat.
    keep = (f'frame-{args.frames // 2}.png', 'screen.png', 'native-panic.json')
    for path in out.iterdir():
        if path.name not in keep:
            path.unlink()
    result_path.write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--libsaturn', type=Path, required=True)
    parser.add_argument('--bios', type=Path, required=True)
    parser.add_argument('--nm', default='sh2eb-elf-nm')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=1500)
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--port2', choices=('pad', 'none'), default='pad',
                        help='none lets the CPU play P2')
    parser.add_argument('--resume', action='store_true', help='keep pairs that already have a result')
    parser.add_argument('--pair', type=int, nargs=2, action='append', help='instead of the default pairs')
    args = parser.parse_args()
    args.build, args.out = args.build.resolve(), args.out.resolve()
    args.out.mkdir(parents=True, exist_ok=True)
    failed = 0
    for first, second in args.pair or default_pairs():
        result = run_pair(args, first, second)
        failed += not result['ok']
        print(json.dumps(result), flush=True)
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main()
