#!/usr/bin/env python3
"""Measure one attract fight on Ymir, cache on, and keep only the numbers.

The Ymir counterpart of mednafen_fight.py, with port 2 empty and no input. For a
fight against one CPU, configure the build with -DT3_DEMO_CPU_MASK=2. --symbol
reports extra ELF symbols as words. The probe's output folder is deleted.

Reports the same rates between tick marks as mednafen_fight.py. Configure the
build with -DT3_TEST_SKIP_INTRO=ON, or the opening movies play out first.

These are emulator timings, not hardware timings.
"""
import argparse
import json
import shutil
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from mednafen_fight import mark_rates  # noqa: E402

DIAGNOSTICS = 'g_t3_diagnostics'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, default=ROOT / 'build/roster')
    p.add_argument('--out', type=Path, required=True, help='new folder for the probe, deleted afterwards')
    p.add_argument('--fields', type=int, default=6000, help='fields from boot; the fight starts near field 1600')
    p.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    p.add_argument('--symbol', action='append', default=[], help='also report this ELF symbol as words')
    args = p.parse_args()
    command = [sys.executable, str(ROOT / 'scripts/saturn_probe.py'), '--build', str(args.build),
               '--disc', str(args.build / 'disc/music.cue'), '--bios', str(args.bios), '--out', str(args.out),
               '--frames', str(args.fields), '--timeout', '900', '--port2', 'none', '--sh2-cache', 'on',
               '--allow-missing-dumps']
    symbols = [DIAGNOSTICS] + [s for s in args.symbol if s != DIAGNOSTICS]
    for symbol in symbols:
        command += ['--dump-symbol', symbol]
    try:
        run = json.loads(subprocess.check_output(command, text=True))
        dumps = json.loads((args.out / 'symbol-dumps.json').read_text())
        words = {}
        for symbol in symbols:
            # saturn_probe.py keys the dump by the ELF name, which has the C compiler's '_' prefix.
            entry = dumps[symbol] if symbol in dumps else dumps['_' + symbol]
            data = (args.out / entry['file']).read_bytes()
            words[symbol] = list(struct.unpack(f'>{len(data) // 4}I', data[:len(data) // 4 * 4]))
    finally:
        shutil.rmtree(args.out, ignore_errors=True)
    state = run['state']
    vdp1, reached = mark_rates(words[DIAGNOSTICS])
    print(json.dumps(dict(**vdp1, marks=reached,
                          images=state['submitted_frames'], observed_fields=state['observed_fields'],
                          images_per_second=round(state['submitted_frames'] * 60 / state['observed_fields'], 2),
                          discarded_fields=state['discarded_fields'],
                          ai_pad_hash=hex(run['optimizations'].get('ai_pad_hash', 0)),
                          symbols={s: words[s] for s in args.symbol})))


if __name__ == '__main__':
    main()
