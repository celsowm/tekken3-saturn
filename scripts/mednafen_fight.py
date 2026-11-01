#!/usr/bin/env python3
"""Measure one fight on Mednafen and keep only the numbers.

Boots the mixed-mode disc with port 2 empty and no input, so it measures the
attract fight, and reads the counters from the save state written at exit. The
recording and the emulator profile are deleted at once (see AGENTS.md).

Key presses have not been seen to reach the game on Mednafen, so this cannot
open ARCADE MODE. For a fight against one CPU, configure the build with
-DT3_DEMO_CPU_MASK=2: P1 stays neutral and only the second fighter is the CPU.

These are emulator timings, not hardware timings.
"""
import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FRT_PER_FIELD = 3728
SYMBOLS = ['_g_tick_clock', '_g_t3_diagnostics', '_g_t3_ai_ticks', '_g_t3_ai_calls', '_g_t3_ai_max_ticks', '_g_t3_ai_pad_hash']
WORD = re.compile(r'^(\S+) \S+ \d+: \[(.*)\]$')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, default=ROOT / 'build/roster')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--seconds', type=float, default=90)
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--shot', type=float, nargs='*', default=[], help='keep a frame at these seconds')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    run = args.out / 'run'
    shutil.rmtree(run, ignore_errors=True)
    command = [sys.executable, str(ROOT / 'scripts/record_mednafen.py'), '--build', str(args.build),
               '--cue', str(args.build / 'disc/music.cue'), '--bios', str(args.bios), '--out', str(run),
               '--neutral-input', '--port2-none', '--save-state', '--seconds', str(args.seconds),
               '--screenshots', *map(str, args.shot)]
    try:
        subprocess.run(command, stdout=subprocess.DEVNULL, check=True)
        state = [sys.executable, str(ROOT / 'scripts/mednafen_state.py'), str(run),
                 '--elf', str(args.build / 'tekken3_saturn.elf'), '--words']
        for symbol in SYMBOLS:
            state += ['--symbol', symbol]
        text = subprocess.check_output(state, text=True)
    finally:
        (run / 'run.mov').unlink(missing_ok=True)
        shutil.rmtree(run / 'mednafen-profile', ignore_errors=True)
    words = {m[1]: [int(x) for x in m[2].split(',')] for line in text.splitlines() if (m := WORD.match(line))}
    _, _, observed, discarded, ticks = words['_g_tick_clock'][:5]
    calls = words['_g_t3_ai_calls'][0]
    images = words['_g_t3_diagnostics'][2]  # frame_serial
    report = dict(seconds=args.seconds, images=images,
                  images_per_second=round(images * 60 / observed, 2) if observed else None,
                  observed_fields=observed, discarded_fields=discarded,
                  discarded_percent=round(discarded * 100 / observed, 2) if observed else None, ticks=ticks,
                  ai_calls=calls, ai_mean_field=round(words['_g_t3_ai_ticks'][0] / calls / FRT_PER_FIELD, 3) if calls else 0,
                  ai_max_field=round(words['_g_t3_ai_max_ticks'][0] / FRT_PER_FIELD, 3),
                  ai_pad_hash=f"{words['_g_t3_ai_pad_hash'][0]:#010x}")
    (args.out / 'fight.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
