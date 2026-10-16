#!/usr/bin/env python3
"""Behavioral acceptance through the external, headless Saturn emulator."""
import copy
import json
from pathlib import Path

import saturn_probe


def execute(args):
    root = args.out.resolve()
    root.mkdir(parents=True, exist_ok=True)
    results = {}

    def case(name, script='', frames=180, port2='pad'):
        options = copy.copy(args)
        options.out = root / name
        options.out.mkdir(parents=True, exist_ok=True)
        options.frames, options.port2 = frames, port2
        options.device_script = options.out / 'input.devices'
        options.device_script.write_text('0 1 buttons=NONE\n0 2 buttons=NONE\n' + script)
        result = saturn_probe.run(options)
        results[name] = result
        return result['state']

    def require(condition, message):
        if not condition:
            (root / 'failed-observations.json').write_text(json.dumps(results, indent=2))
            raise AssertionError(message)

    neutral = case('neutral')
    require(neutral['fighter_x'] == [-360, 360] and neutral['health'] == [1000, 1000],
            'connected neutral pads must suppress CPU movement and damage')
    repeated = case('neutral-repeat')
    require(neutral == repeated, 'identical runs must reproduce every diagnostic field')
    approach = case('approach', '20 1 buttons=RIGHT\n60 1 buttons=NONE\n')
    require(approach['fighter_x'][0] > -360, 'RIGHT must approach from initial P1 facing')
    retreat = case('retreat', '20 1 buttons=LEFT\n60 1 buttons=NONE\n')
    require(retreat['fighter_x'][0] < -360, 'LEFT must retreat from initial P1 facing')
    for direction, sign in [('UP', -1), ('DOWN', 1)]:
        s = case('sidestep-' + direction.lower(), f'20 1 buttons={direction}\n60 1 buttons=NONE\n')
        require(s['fighter_z'][0] * sign > 0, 'sidestep must change Z')
    for button, field in [('L', 'guarding'), ('R', 'crouching'), ('L+R', 'crouching')]:
        s = case('held-' + button.replace('+', '-'), f'20 1 buttons={button}\n')
        require(s[field][0] == 1 and s['pad_held'][0] != 0,
                f'{button} must reach the sampled pad and fighter')
    p2 = case('second-pad', '20 2 buttons=LEFT\n60 2 buttons=NONE\n')
    require(p2['fighter_x'][1] < 360 and p2['fighter_x'][0] == -360,
            'second pad must independently control P2')
    cpu = case('cpu', port2='none')
    require(cpu['pad_connected'][1] == 0 and cpu['fighter_x'][1] != 360,
            'disconnected P2 must enable CPU')
    close = '20 1 buttons=RIGHT\n60 1 buttons=NONE\n'
    for button, damage in [('X', 70), ('Y', 95), ('A', 115), ('B', 145)]:
        s = case('attack-' + button, close + f'90 1 buttons={button}\n')
        require(s['hits'] == 1 and s['health'][1] == 1000 - damage and s['attack'][0] == 0,
                f'{button} must cause exactly one prototype hit while held')
        s = case('repress-' + button, close +
                 f'90 1 buttons={button}\n150 1 buttons=NONE\n170 1 buttons={button}\n', 250)
        require(s['hits'] == 2 and s['health'][1] == 1000 - 2 * damage,
                f'{button} release/repress must cause a second hit')
    for button, defense in [('X', 'L'), ('A', 'L'), ('B', 'L+R')]:
        s = case('block-' + button, close + f'0 2 buttons={defense}\n90 1 buttons={button}\n')
        require(s['blocks'] == 1 and s['hits'] == 0 and s['health'][1] == 1000,
                f'{defense} must block {button}')
    reset = case('reset', close + '90 1 buttons=X\n200 1 buttons=START\n220 1 buttons=NONE\n', 260)
    require(reset['fighter_x'] == [-360, 360] and reset['health'] == [1000, 1000]
            and reset['hits'] == 0 and reset['round'] == 1 and reset['sim_ticks'] < 100,
            'START must restore match state')
    (root / 'suite.json').write_text(json.dumps(dict(passed=True, cases=results), indent=2) + '\n')
    print(f'{len(results)} emulator scenarios passed; evidence: {root / "suite.json"}')


if __name__ == '__main__':
    p = saturn_probe.parser()
    p.set_defaults(out=saturn_probe.ROOT / 'assets/imported/saturn-input-suite')
    execute(p.parse_args())
