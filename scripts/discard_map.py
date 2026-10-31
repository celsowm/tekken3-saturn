#!/usr/bin/env python3
"""List where the tick clock discards fields on Ymir, grouped into clusters.

The probe prints only 64 watch events per run, so this resumes from the last
event until the run ends. It reports emulator timing, not hardware timing.
"""
import argparse
import json
import re
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EVENT = re.compile(r'\s*frame (\d+) pc \S+ addr \S+ 32-bit value (\w+)')
DISCARDED_OFFSET = 12  # t3_tick_clock_t.discarded_fields
CLUSTER_GAP = 30


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'build/roster')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--frames', type=int, default=6000)
    parser.add_argument('--probe', type=Path, default=Path.home() / 'dev/lib-saturn/harness/build-macos/probe')
    parser.add_argument('--bios', type=Path,
                        default=ROOT / 'assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin')
    parser.add_argument('--nm', default=shutil.which('sh2eb-elf-nm') or str(ROOT.parent / 'saturn-tools/bin/sh2eb-elf-nm'))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    devices = args.out / 'none.devices'
    devices.write_text('0 1 buttons=NONE\n0 2 buttons=NONE\n')
    table = subprocess.check_output([args.nm, str(args.build / 'tekken3_saturn.elf')], text=True)
    clock = next(int(line.split()[0], 16) for line in table.splitlines() if line.endswith('_g_tick_clock'))
    events, start = {}, 0
    while True:
        log = args.out / f'from{start}.log'
        with log.open('w') as stream:
            subprocess.run([str(args.probe), '--iso', str(args.build / 'disc/music.cue'),
                            '--bin', str(args.build / 'tekken3_saturn.app.bin'), '--bios', str(args.bios),
                            '--frames', str(args.frames), '--device-script', str(devices), '--sh2-cache', 'on',
                            '--watch-write', f'{clock + DISCARDED_OFFSET:#x}:4:{start}',
                            '--out', str(args.out / 'probe.json')], stdout=stream, stderr=subprocess.STDOUT, check=False)
        found = [(int(m[1]), int(m[2], 16)) for line in log.read_text().splitlines() if (m := EVENT.match(line))]
        events.update(found)
        if not found or found[-1][0] <= start:
            break
        start = found[-1][0]
    fields = sorted(events)
    previous, rows = 0, []
    for field in fields:
        rows.append((field, events[field] - previous))
        previous = events[field]
    clusters = []
    for field, amount in rows:
        if clusters and field - clusters[-1]['last'] <= CLUSTER_GAP:
            clusters[-1]['last'] = field
            clusters[-1]['discarded'] += amount
            clusters[-1]['events'].append(amount)
        else:
            clusters.append(dict(first=field, last=field, discarded=amount, events=[amount]))
    (args.out / 'discards.json').write_text(json.dumps(dict(total=previous, clusters=clusters), indent=1) + '\n')
    print('total', previous)
    for c in clusters:
        print(f"{c['first']:>5}-{c['last']:<5} discarded {c['discarded']:>3}  {c['events']}")


if __name__ == '__main__':
    main()
