#!/usr/bin/env python3
"""Measure image cadence around strong-hit particle bursts on Ymir.

Finds the bursts by watching the drawn-particle counter, then watches image
submissions around each one. It reports emulator timing, not hardware timing.
"""
import argparse
import json
import re
import shutil
import subprocess
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EVENT = re.compile(r'\s*frame (\d+) pc \S+ addr \S+ 32-bit value (\w+)')
BURST_GAP = 30


def symbol(elf, nm, name):
    for line in subprocess.check_output([nm, str(elf)], text=True).splitlines():
        fields = line.split()
        if fields[-1] in (name, '_' + name):
            return int(fields[0], 16)
    raise SystemExit(f'{name} not in {elf}')


def run_watch(args, out, name, address, last, start=0):
    log = out / f'{name}.log'
    with log.open('w') as stream:
        subprocess.run([str(args.probe), '--iso', str(args.disc), '--bin', str(args.bin), '--bios', str(args.bios),
                        '--frames', str(last), '--device-script', str(out / 'none.devices'),
                        '--watch-write', f'{address:#x}:4:{start}', '--out', str(out / f'{name}.json')],
                       stdout=stream, stderr=subprocess.STDOUT, check=False)
    return [(int(m[1]), int(m[2], 16)) for line in log.read_text().splitlines() if (m := EVENT.match(line))]


def bursts_from(events):
    groups = []
    # The counter is zeroed at boot; a real burst spans many fields.
    for field, _ in events:
        if groups and field - groups[-1][1] <= BURST_GAP:
            groups[-1][1] = field
        else:
            groups.append([field, field])
    return [group for group in groups if group[1] - group[0] >= 10]


def stats(intervals):
    if not intervals:
        return dict(images=0, fields=0, images_per_second=None, max_interval=None, discarded=0, intervals=[])
    fields = sum(intervals)
    return dict(images=len(intervals), fields=fields, images_per_second=round(60 * len(intervals) / fields, 1),
                max_interval=max(intervals), discarded=sum(max(0, d - args_cap) for d in intervals),
                intervals=intervals)


args_cap = 4


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/roster')
    parser.add_argument('--bin', type=Path, help='defaults to the build app binary')
    parser.add_argument('--elf', type=Path, help='defaults to the build ELF')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=6000)
    parser.add_argument('--probe', type=Path, default=Path.home() / 'dev/lib-saturn/harness/build-macos/probe')
    parser.add_argument('--bios', type=Path,
                        default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    parser.add_argument('--nm', default=shutil.which('sh2eb-elf-nm') or str(ROOT.parent / 'saturn-tools/bin/sh2eb-elf-nm'))
    args = parser.parse_args()
    args.disc = args.build / 'disc/music.cue'
    args.bin = args.bin or args.build / 'tekken3_saturn.app.bin'
    args.elf = args.elf or args.build / 'tekken3_saturn.elf'
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out / 'none.devices').write_text('0 1 buttons=NONE\n0 2 buttons=NONE\n')
    drawn = symbol(args.elf, args.nm, 'g_t3_ordered_mask_drawn_images')
    submissions = symbol(args.elf, args.nm, 'g_render_submissions')
    bursts = bursts_from(run_watch(args, args.out, 'drawn', drawn, args.frames))
    def measure(index_burst):
        index, (first, last) = index_burst
        start = max(0, first - 50)
        events = run_watch(args, args.out, f'burst{index}', submissions, last + 60, start)
        intervals = [(f2, f2 - f1) for (f1, v1), (f2, v2) in zip(events, events[1:]) if v2 == v1 + 1]
        return dict(first=first, last=last,
                    before=stats([d for f, d in intervals if f < first]),
                    during=stats([d for f, d in intervals if first < f <= last]))
    with ThreadPoolExecutor(max_workers=5) as pool:
        result = dict(bin=str(args.bin), bursts=list(pool.map(measure, enumerate(bursts))))
    (args.out / 'cadence.json').write_text(json.dumps(result, indent=1) + '\n')
    for burst in result['bursts']:
        before, during = burst['before'], burst['during']
        print(f"{burst['first']:>5} before {before['images_per_second']} /s  during {during['images_per_second']} /s  "
              f"discarded {during['discarded']}/{during['fields']}  {during['intervals']}")


if __name__ == '__main__':
    main()
