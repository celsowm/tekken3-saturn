#!/usr/bin/env python3
"""KO the idle P2 with one boot-fighter pair and keep pictures of P1's win pose.

Reconfigures --build in place and restores it (AGENTS.md disk budget). Leaves
screenshots and the last state as JSON under --out, one directory per button;
the button held at the end of the replay picks the pose. --dump-symbol passes
through to the probe for the last run of each pose.
"""
import copy
import json
import shutil
import subprocess
from pathlib import Path

import saturn_combat_suite as suite
import saturn_probe

SHOT_FIELDS = (30, 90, 150, 210, 270, 330, 390, 450)


def run_case(args, out, script, frames, shots=(), dump=()):
    a = copy.copy(args)
    a.out = out
    out.mkdir(parents=True, exist_ok=True)
    a.frames, a.port2, a.device_script = frames, 'none' if args.cpu else 'pad', out / 'input.devices'
    a.screenshot_frame, a.dump_symbol = list(shots), list(dump)
    a.device_script.write_text('0 1 buttons=NONE\n0 2 buttons=NONE\n' + script)
    return suite.coherent_case(a)['state']


def knockout(args, work):
    """The pad tape that wins the match, and the field it is sure to be over."""
    start = suite.first_gameplay_field(args) + suite.GAMEPLAY_LEAD_FIELDS
    lines, state = [], None
    for wins in range(1, args.rounds + 1):
        for i in range(args.approaches):
            roots = state['psx_world_root'] if state else [-1, 0, 0, 1]
            direction = 'RIGHT' if roots[0] < roots[3] else 'LEFT'
            t = start + 180 * i
            lines += [f'{t} 1 buttons={direction}', f'{t + 60} 1 buttons=NONE',
                      f'{t + 68} 1 buttons=DOWN+A', f'{t + 138} 1 buttons=NONE']
            state = run_case(args, work, '\n'.join(lines) + '\n', t + 175)
            print(f'round {wins} approach {i}: hp {state["psx_health_q16"]}', flush=True)
            if state['wins0'] == wins and state['round_done']:
                break
        else:
            raise SystemExit('the low attacks did not finish the round')
        end = t + 175
        if wins == args.rounds:
            return '\n'.join(lines) + '\n', end
        for start in range(end + 300, end + 2400, 60):
            state = run_case(args, work, '\n'.join(lines) + '\n', start)
            if state['wins0'] == wins and state['psx_round_state'] == 1:
                break
        else:
            raise SystemExit('the next round did not start')


def pose(args, tape, end, button, work):
    """Find the pose's first field with the button held, then photograph it."""
    script = tape + f'{end} 1 buttons={button}\n'
    for probe in range(300, 1801, 60):
        state = run_case(args, work, script, end + probe)
        if state['psx_round_state'] == 6:
            break
    else:
        raise SystemExit(f'{button}: no win pose')
    first = end + probe
    last = first + SHOT_FIELDS[-1] + 1
    out = args.out / button
    state = run_case(args, out, script, last, [first + d for d in SHOT_FIELDS], args.dump_symbol)
    (out / 'state.json').write_text(json.dumps(state, indent=1) + '\n')
    for path in out.iterdir():
        if path.suffix not in ('.png', '.json') and not path.name.endswith('.bin'):
            path.unlink() if path.is_file() else shutil.rmtree(path)
    print(button, 'slot', state['psx_move_slot'], 'frame', state['psx_move_frame'],
          'faces', state['faces_submitted'], 'culled', state['faces_culled'], 'skipped', state['faces_skipped'],
          'work ms', state['frame_work_ms'], '->', out, flush=True)
    if state['faces_skipped']:
        raise SystemExit(f'{button}: the win pose lost {state["faces_skipped"]} faces')


def cpu_wins(args, work):
    """Leave P1 idle until the CPU on port 2 has won the match, then photograph its pose."""
    for field in range(3000, 30000, 300):
        state = run_case(args, work, '', field)
        print(f'field {field}: hp {state["psx_health_q16"]} wins {state["wins0"]},{state["wins1"]}'
              f' round state {state["psx_round_state"]}', flush=True)
        if state['wins1'] == 2 and state['psx_round_state'] == 6:
            break
    else:
        raise SystemExit('the CPU did not win')
    out = args.out / 'cpu'
    state = run_case(args, out, '', field + 601, [field + 60 * i for i in range(11)], args.dump_symbol)
    (out / 'state.json').write_text(json.dumps(state, indent=1) + '\n')
    for path in out.iterdir():
        if path.suffix not in ('.png', '.json') and not path.name.endswith('.bin'):
            path.unlink() if path.is_file() else shutil.rmtree(path)
    print('cpu slot', state['psx_move_slot'], 'frame', state['psx_move_frame'], '->', out, flush=True)


def main():
    parser = saturn_probe.parser()
    parser.add_argument('--fighters', type=int, nargs=2, default=(3, 1))
    parser.add_argument('--buttons', nargs='+', default=['X', 'Y', 'A', 'B'])
    parser.add_argument('--approaches', type=int, default=40)
    parser.add_argument('--rounds', type=int, default=2, help='rounds to win; two ends the match')
    parser.add_argument('--cpu', action='store_true', help='the CPU on port 2 beats an idle P1 instead')
    parser.set_defaults(build=saturn_probe.ROOT / 'build/roster',
                        out=saturn_probe.ROOT / 'assets/imported/win-pose-probe')
    args = parser.parse_args()
    args.disc = args.disc or args.build / 'disc/music.cue'
    first, second = args.fighters
    subprocess.run(['cmake', '.', f'-DT3_BOOT_FIGHTERS={first};{second}'], cwd=args.build,
                   check=True, stdout=subprocess.DEVNULL)
    try:
        # After a fighter change the first disc step can see a stale 0.BIN; the second pass is clean.
        if subprocess.run(['ninja'], cwd=args.build, stdout=subprocess.DEVNULL).returncode:
            subprocess.run(['ninja'], cwd=args.build, check=True, stdout=subprocess.DEVNULL)
        work = args.out / 'work'
        if args.cpu:
            cpu_wins(args, work)
            shutil.rmtree(work)
            return
        saved = args.out / f'knockout-{first}-{second}.json'
        tape, end = json.loads(saved.read_text()) if saved.is_file() else knockout(args, work)
        saved.write_text(json.dumps([tape, end]) + '\n')
        for button in args.buttons:
            pose(args, tape, end, button, work)
        shutil.rmtree(work)
    finally:
        subprocess.run(['cmake', '.', '-DT3_BOOT_FIGHTERS='], cwd=args.build,
                       check=True, stdout=subprocess.DEVNULL)


if __name__ == '__main__':
    main()
