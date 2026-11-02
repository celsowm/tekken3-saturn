#!/usr/bin/env python3
"""Count the sound requests of scripted headless fights (Ymir) per sound, to size which effects a fight needs.

Each scenario is a seeded P1 pad script against the CPU as P2 (port 2 empty). The build must have the per-sound
counters of src/saturn/sfx.c (g_t3_sfx_shared_plays / g_t3_sfx_voice_plays). Output: one JSON with the counters of
every run, written under assets/imported/ (git-ignored). Counting requests says how often a sound is wanted;
it says nothing about how many play at once (polyphony)."""
import argparse
import json
import random
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ATTACKS = ('X', 'Y', 'A', 'B')
STYLES = ('idle', 'block', 'mash', 'rush', 'poke', 'mixed')


def script(style, seed, fields, start):
    """Pad lines '<field> 1 buttons=...'; P1 faces right, so RIGHT is forward and LEFT is back (guard)."""
    rng = random.Random(f'{style}:{seed}')
    lines = ['0 1 buttons=NONE', '0 2 buttons=NONE']
    t = start
    while t < fields and style != 'idle':
        if style == 'block':
            hold = rng.choice(('LEFT', 'LEFT', 'DOWN+LEFT'))
            lines += [f'{t} 1 buttons={hold}', f'{t + rng.randint(40, 120)} 1 buttons=NONE']
            t += rng.randint(60, 160)
            continue
        direction = {'mash': ('NONE', 'RIGHT'), 'rush': ('RIGHT', 'RIGHT'), 'poke': ('NONE', 'LEFT'),
                     'mixed': ('NONE', 'RIGHT', 'LEFT', 'DOWN', 'UP', 'DOWN+RIGHT')}[style]
        d = rng.choice(direction)
        a = rng.choice(ATTACKS + (('X+A', 'Y+B') if style == 'mixed' else ()))
        keys = a if d == 'NONE' else d + '+' + a
        lines += [f'{t} 1 buttons={keys}', f'{t + 2} 1 buttons={d}']
        t += rng.randint(4, 14) if style in ('mash', 'rush') else rng.randint(12, 40)
        if rng.random() < 0.08:
            lines += [f'{t} 1 buttons=NONE']
            t += rng.randint(20, 60)
    lines.append(f'{fields} 1 buttons=NONE')
    return '\n'.join(lines) + '\n'


def run(build, bios, out, style, seed, fields, start, nm):
    out.mkdir(parents=True, exist_ok=True)
    pad = out / 'input.devices'
    pad.write_text(script(style, seed, fields, start))
    command = [sys.executable, str(ROOT / 'scripts/saturn_probe.py'), '--build', str(build), '--bios', str(bios),
               '--frames', str(fields), '--port2', 'none', '--device-script', str(pad), '--out', str(out),
               '--timeout', '900'] + (['--nm', nm] if nm else [])
    done = subprocess.run(command, capture_output=True, text=True)
    observation = out / 'observation.json'
    if done.returncode or not observation.is_file():
        return dict(style=style, seed=seed, error=(done.stderr or done.stdout)[-400:])
    o = json.loads(observation.read_text())['optimizations']
    keep = ('sfx_shared_plays', 'sfx_voice_plays', 'sfx_played', 'sfx_unmapped', 'sfx_unmapped_seen', 'event_hits',
            'event_blocks', 'event_swings', 'event_sounds', 'sfx_ram_used', 'sfx_ram_capacity', 'sfx_resident_sounds',
            'sfx_voice_steals', 'sfx_failed_plays')
    return dict(style=style, seed=seed, fields=fields, **{k: o.get(k) for k in keep})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    parser.add_argument('--out', type=Path, default=ROOT / 'assets/imported/sfxreq')
    parser.add_argument('--fields', type=int, default=5400)
    parser.add_argument('--start', type=int, default=400, help='first field with a controllable fight')
    parser.add_argument('--styles', nargs='+', default=list(STYLES), choices=STYLES)
    parser.add_argument('--seeds', type=int, default=2)
    parser.add_argument('--nm')
    args = parser.parse_args()
    runs = []
    for style in args.styles:
        for seed in range(args.seeds):
            result = run(args.build, args.bios, args.out / f'{style}-{seed}', style, seed, args.fields, args.start, args.nm)
            runs.append(result)
            print(style, seed, {k: v for k, v in result.items() if k in ('sfx_played', 'event_hits', 'event_swings', 'error')},
                  file=sys.stderr, flush=True)
    (args.out / 'runs.json').write_text(json.dumps(runs, indent=1) + '\n')


if __name__ == '__main__':
    main()
