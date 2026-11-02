#!/usr/bin/env python3
"""How many events per tick can the 68000 run inside one tick? Builds the paged prototype on synthetic streams
(N harmless register writes in every tick between two audible clicks), runs each on Ymir and reports whether the
driver tick kept up with the sample clock. Needs the saturn toolchain on PATH and the Ymir probe."""
import argparse
import json
import os
import struct
import subprocess
from pathlib import Path

import c352_scsp_timing_probe as probe
import run_c352_scsp_prototype as runner

ROOT = Path(__file__).resolve().parents[1]


def run_case(per_tick, tick_samples, ticks, out, args):
    data = out / f'n{per_tick}_t{tick_samples}'
    info = probe.generate_stress(data / 'data', per_tick, tick_samples, ticks)
    build = out / 'build'
    toolchain = args.prefix / 'share/libsaturn/toolchains/sh2eb-elf.cmake'
    runner.sh(['cmake', '-S', str(ROOT / 'tests/c352_scsp_proto'), '-B', str(build), '-G', 'Ninja',
               f'-DCMAKE_TOOLCHAIN_FILE={toolchain}', f'-DCMAKE_PREFIX_PATH={args.prefix}', '-DCMAKE_BUILD_TYPE=Release',
               f'-DC352_DATA_DIR={data / "data"}'])
    runner.sh(['cmake', '--build', str(build)])
    address = runner.symbol(build / 'c352_scsp_proto.elf', 'g_c352_proto', args.nm)
    audio = data / 'audio.s16le'
    seconds = ticks * tick_samples / 44100 + 0.6
    frames = int(seconds * 59.94 + 200)
    runner.sh([str(args.probe), '--iso', str(args.iso), '--bin', str(build / 'c352_scsp_proto.app.bin'), '--bios',
               str(args.bios), '--boot-frames', '90', '--frames', str(frames), '--out', str(data / 'ymir.json'),
               '--dump-mem', f'{address:#x}:{4 * len(runner.RESULT_FIELDS)}:{data / "results.bin"}',
               '--dump-audio', str(audio)])
    values = struct.unpack(f'>{len(runner.RESULT_FIELDS)}I', (data / 'results.bin').read_bytes())
    r = dict(zip(runner.RESULT_FIELDS, values))
    clicks = probe.analyze_stress(audio, 4, ticks - 8, tick_samples)
    audio.unlink()
    return dict(per_tick=per_tick, tick_samples=tick_samples, events=info['events'], pages=info['pages'],
                executed=r['pg_executed'], late_events=r['pg_late_events'], max_lateness=r['pg_max_lateness'],
                underrun_ticks=r['pg_underrun_ticks'], done=r['pg_done'], max_burst=r['pg_max_burst'], clicks=clicks)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--per-tick', type=int, nargs='+', default=[16, 32, 64, 96, 128, 192, 256])
    parser.add_argument('--tick-samples', type=int, nargs='+', default=[256, 128])
    parser.add_argument('--ticks', type=int, default=240, help='stream length in ticks of 256 samples (scaled for 128)')
    parser.add_argument('--prefix', type=Path, default=Path(os.environ.get('LIBSATURN_PREFIX', Path.home() / 'dev/saturn-prefix')))
    parser.add_argument('--probe', type=Path, default=Path.home() / 'dev/lib-saturn/harness/build-macos/probe')
    parser.add_argument('--bios', type=Path, default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    parser.add_argument('--iso', type=Path, default=ROOT / 'build/disc/tekken3_saturn.iso')
    parser.add_argument('--nm', default='sh2eb-elf-nm')
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    results = []
    for ts in args.tick_samples:
        for n in args.per_tick:
            res = run_case(n, ts, args.ticks * 256 // ts, args.out, args)
            print(json.dumps(res), flush=True)
            results.append(res)
    (args.out / 'stress.json').write_text(json.dumps(results, indent=2))


if __name__ == '__main__':
    main()
