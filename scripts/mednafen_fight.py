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
MARKS = 8  # T3_TICK_MARKS
SYMBOLS = ['_g_tick_clock', '_g_t3_diagnostics', '_g_t3_ai_ticks', '_g_t3_ai_calls', '_g_t3_ai_max_ticks', '_g_t3_ai_pad_hash']
WORD = re.compile(r'^(\S+) \S+ \d+: \[(.*)\]$')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, default=ROOT / 'build/roster')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--seconds', type=float, default=90)
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--shot', type=float, nargs='*', default=[], help='keep a frame at these seconds')
    p.add_argument('--symbol', action='append', default=[], help='also report this ELF symbol as words')
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
        for symbol in SYMBOLS + args.symbol:
            state += ['--symbol', symbol]
        text = subprocess.check_output(state, text=True)
    finally:
        (run / 'run.mov').unlink(missing_ok=True)
        shutil.rmtree(run / 'mednafen-profile', ignore_errors=True)
    words = {m[1]: [int(x) for x in m[2].split(',')] for line in text.splitlines() if (m := WORD.match(line))}
    _, _, observed, discarded, ticks = words['_g_tick_clock'][:5]
    calls = words['_g_t3_ai_calls'][0]
    diagnostics = words['_g_t3_diagnostics']
    images = diagnostics[2]  # frame_serial
    # t3_diagnostics_t words 14-16: late_swaps, draw_waits (the CPU waited for VDP1), draw_timeouts;
    # version 25 adds the VDP1 texture VRAM left free, version 26 then the images and fields at
    # eight tick marks (diagnostics.h, T3_TICK_MARK_STEP ticks apart).
    version = diagnostics[1]
    marks = MARKS if version >= 26 else 0
    vdp1 = dict(late_swaps=diagnostics[14], draw_waits=diagnostics[15], draw_timeouts=diagnostics[16],
                vdp1_vram_free=diagnostics[-1 - 2 * marks] if version >= 25 else None)
    # Images per second between consecutive reached marks: the same stretch of the deterministic
    # fight in every build, without the loading time a whole-lifetime rate includes.
    segments = []
    reached = []
    if marks:
        at_images, at_fields = diagnostics[-2 * marks:-marks], diagnostics[-marks:]
        reached = [(i, f) for i, f in zip(at_images, at_fields) if i]
        segments = [round((i1 - i0) * 60 / (f1 - f0), 2) for (i0, f0), (i1, f1) in zip(reached, reached[1:])]
        span = reached[-1][1] - reached[0][1] if len(reached) > 1 else 0
        vdp1.update(segment_images_per_second=segments,
                    marked_images_per_second=round((reached[-1][0] - reached[0][0]) * 60 / span, 2) if span else None,
                    worst_segment=min(segments) if segments else None)
    report = dict(seconds=args.seconds, images=images, **vdp1,
                  marks=reached,
                  images_per_second=round(images * 60 / observed, 2) if observed else None,
                  observed_fields=observed, discarded_fields=discarded,
                  discarded_percent=round(discarded * 100 / observed, 2) if observed else None, ticks=ticks,
                  ai_calls=calls, ai_mean_field=round(words['_g_t3_ai_ticks'][0] / calls / FRT_PER_FIELD, 3) if calls else 0,
                  ai_max_field=round(words['_g_t3_ai_max_ticks'][0] / FRT_PER_FIELD, 3),
                  ai_pad_hash=f"{words['_g_t3_ai_pad_hash'][0]:#010x}", symbols={s: words[s] for s in args.symbol})
    (args.out / 'fight.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
